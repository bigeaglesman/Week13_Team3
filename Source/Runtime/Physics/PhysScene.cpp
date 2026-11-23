#include "EnginePCH.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXPublic.h"
#include "Collision/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "GameFramework/Actor.h"
#include "Render/LineBatcher.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <unordered_set>
#include <vector>

using namespace physx;

namespace
{
	// 6단계 Scene Query 필터 콜백. 7단계에서 PxFilterData 채널 도입 전까지
	// 컴포넌트의 IgnoreActor / bBlockComponent / IsShown(true) 조건을 검사한다.
	class FPhysXQueryFilterCallback : public PxQueryFilterCallback
	{
	public:
		FPhysXQueryFilterCallback(const UWorld* InWorld, const AActor* InIgnoreActor, bool bInBlockingOnly)
			: OwningWorld(InWorld)
			, IgnoreActor(InIgnoreActor)
			, bBlockingOnly(bInBlockingOnly)
		{
		}

		PxQueryHitType::Enum preFilter(
			const PxFilterData& FilterData,
			const PxShape* Shape,
			const PxRigidActor* Actor,
			PxHitFlags& QueryFlags) override
		{
			(void)FilterData;
			(void)QueryFlags;

			UPrimitiveComponent* Component = nullptr;
			if (Shape && Shape->userData)
			{
				Component = static_cast<UPrimitiveComponent*>(Shape->userData);
			}
			else if (Actor && Actor->userData)
			{
				Component = static_cast<UPrimitiveComponent*>(Actor->userData);
			}

			if (!Component || !Component->IsRegistered() || Component->GetWorld() != OwningWorld)
			{
				return PxQueryHitType::eNONE;
			}

			AActor* Owner = Component->GetOwner();
			if (!Owner || Owner == IgnoreActor || !Owner->GetActorEnableCollision())
			{
				return PxQueryHitType::eNONE;
			}

			if (bBlockingOnly)
			{
				if (!Component->bBlockComponent)
				{
					return PxQueryHitType::eNONE;
				}
			}
			else
			{
				// 트리거 전용 도형(UShapeComponent이면서 bBlockComponent=false)은 이동/바닥 Sweep에서 무시한다.
				if (Cast<UShapeComponent>(Component))
				{
					if (!Component->bBlockComponent)
					{
						return PxQueryHitType::eNONE;
					}
				}
				else
				{
					// 메시는 보이는 컴포넌트만 충돌한다 (기존 Scene.BVH 동작 유지).
					if (!Component->IsShown(true))
					{
						return PxQueryHitType::eNONE;
					}
				}
			}

			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(
			const PxFilterData& FilterData,
			const PxQueryHit& Hit,
			const PxShape* Shape,
			const PxRigidActor* Actor) override
		{
			(void)FilterData;
			(void)Hit;
			(void)Shape;
			(void)Actor;
			return PxQueryHitType::eBLOCK;
		}

	private:
		const UWorld* OwningWorld = nullptr;
		const AActor* IgnoreActor = nullptr;
		bool bBlockingOnly = false;
	};

	// 7단계 커스텀 시뮬레이션 필터 셰이더.
// word0 = 자신의 채널 비트, word1 = Block 마스크, word2 = Overlap 마스크, word3 = ActorUniqueID
	PxFilterFlags PhysXSimulationFilterShader(
		PxFilterObjectAttributes Attributes0, PxFilterData FilterData0,
		PxFilterObjectAttributes Attributes1, PxFilterData FilterData1,
		PxPairFlags& PairFlags, const void* ConstantBlock, PxU32 ConstantBlockSize)
	{
		(void)ConstantBlock;
		(void)ConstantBlockSize;

		// 같은 Actor 소속 컴포넌트끼리는 충돌·겹침을 무시한다.
		if (FilterData0.word3 != 0 && FilterData0.word3 == FilterData1.word3)
		{
			return PxFilterFlag::eSUPPRESS;
		}

		const bool b0Blocks1 = (FilterData0.word1 & FilterData1.word0) != 0;
		const bool b1Blocks0 = (FilterData1.word1 & FilterData0.word0) != 0;
		const bool b0Overlaps1 = (FilterData0.word2 & FilterData1.word0) != 0;
		const bool b1Overlaps0 = (FilterData1.word2 & FilterData0.word0) != 0;

		// 둘 중 하나라도 Trigger Shape(eTRIGGER_SHAPE)인 경우
		if (PxFilterObjectIsTrigger(Attributes0) || PxFilterObjectIsTrigger(Attributes1))
		{
			if (b0Overlaps1 && b1Overlaps0)
			{
				PairFlags = PxPairFlag::eTRIGGER_DEFAULT;
				return PxFilterFlag::eDEFAULT;
			}
			return PxFilterFlag::eSUPPRESS;
		}

		// 둘 다 일반 Simulation Shape: 양쪽 모두 Block이어야 물리 충돌 해결 + Hit 알림
		if (b0Blocks1 && b1Blocks0)
		{
			PairFlags = PxPairFlag::eCONTACT_DEFAULT
				| PxPairFlag::eNOTIFY_TOUCH_FOUND
				| PxPairFlag::eNOTIFY_TOUCH_LOST;
			return PxFilterFlag::eDEFAULT;
		}

		// 물리 Block은 아니지만 양쪽 모두 Ignore가 아니고 한쪽 이상 Overlap이면 비물리 접촉 감지만 수행
		if ((b0Blocks1 || b0Overlaps1) && (b1Blocks0 || b1Overlaps0) && (b0Overlaps1 || b1Overlaps0))
		{
			PairFlags = PxPairFlag::eDETECT_DISCRETE_CONTACT
				| PxPairFlag::eNOTIFY_TOUCH_FOUND
				| PxPairFlag::eNOTIFY_TOUCH_LOST;
			return PxFilterFlag::eDEFAULT;
		}

		return PxFilterFlag::eSUPPRESS;
	}
}

struct FPendingOverlapEvent
{
	UPrimitiveComponent* CompA = nullptr;
	UPrimitiveComponent* CompB = nullptr;
	bool bBegin = true;
};

struct FPendingHitEvent
{
	UPrimitiveComponent* CompA = nullptr;
	UPrimitiveComponent* CompB = nullptr;
};

// PhysX fetchResults() 내부에서 호출되는 이벤트 콜백.
// 콜백 안에서는 절대 씬을 수정하지 않고 큐에만 쌓아 둔다.
class FPhysXSimulationEventCallback : public PxSimulationEventCallback
{
public:
	std::vector<FPendingOverlapEvent> PendingOverlaps;
	std::vector<FPendingHitEvent> PendingHits;

	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

	void onTrigger(PxTriggerPair* Pairs, PxU32 Count) override
	{
		for (PxU32 i = 0; i < Count; ++i)
		{
			const PxTriggerPair& Pair = Pairs[i];

			// 삭제된 Shape/Actor 포인터는 역참조하면 크래시가 나므로 건너뛴다.
			if (Pair.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
			{
				continue;
			}

			UPrimitiveComponent* CompA = ExtractComponent(Pair.triggerShape, Pair.triggerActor);
			UPrimitiveComponent* CompB = ExtractComponent(Pair.otherShape, Pair.otherActor);
			if (!CompA || !CompB || CompA == CompB)
			{
				continue;
			}

			if (Pair.status == PxPairFlag::eNOTIFY_TOUCH_FOUND)
			{
				PendingOverlaps.push_back({ CompA, CompB, true });
			}
			else if (Pair.status == PxPairFlag::eNOTIFY_TOUCH_LOST)
			{
				PendingOverlaps.push_back({ CompA, CompB, false });
			}
		}
	}

	void onContact(const PxContactPairHeader& PairHeader, const PxContactPair* Pairs, PxU32 NbPairs) override
	{
		if (PairHeader.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
		{
			return;
		}

		for (PxU32 i = 0; i < NbPairs; ++i)
		{
			const PxContactPair& Pair = Pairs[i];
			if (Pair.flags & (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1))
			{
				continue;
			}

			UPrimitiveComponent* CompA = ExtractComponent(Pair.shapes[0], PairHeader.actors[0]);
			UPrimitiveComponent* CompB = ExtractComponent(Pair.shapes[1], PairHeader.actors[1]);
			if (!CompA || !CompB || CompA == CompB)
			{
				continue;
			}

			const PxFilterData FD0 = Pair.shapes[0]->getSimulationFilterData();
			const PxFilterData FD1 = Pair.shapes[1]->getSimulationFilterData();
			const bool bIsBlockingPair =
				((FD0.word1 & FD1.word0) != 0) && ((FD1.word1 & FD0.word0) != 0);

			if (bIsBlockingPair)
			{
				if (Pair.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)
				{
					PendingHits.push_back({ CompA, CompB });
				}
			}
			else
			{
				if (Pair.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)
				{
					PendingOverlaps.push_back({ CompA, CompB, true });
				}
				else if (Pair.events & PxPairFlag::eNOTIFY_TOUCH_LOST)
				{
					PendingOverlaps.push_back({ CompA, CompB, false });
				}
			}
		}
	}

private:
	static UPrimitiveComponent* ExtractComponent(const PxShape* Shape, const PxActor* Actor)
	{
		if (Shape && Shape->userData)
		{
			return static_cast<UPrimitiveComponent*>(Shape->userData);
		}
		if (Actor && Actor->userData)
		{
			return static_cast<UPrimitiveComponent*>(Actor->userData);
		}
		return nullptr;
	}
};

FPhysScene::FPhysScene(UWorld* InOwningWorld)
	: OwningWorld(InOwningWorld)
{
	if (!GPhysXSDK || !GPhysXCpuDispatcher)
	{
		HTR_LOG(Error, "[PhysX] Cannot create FPhysScene: PhysX is not initialized");
		return;
	}
	EventCallback = new FPhysXSimulationEventCallback();

	PxSceneDesc SceneDesc(GPhysXSDK->getTolerancesScale());
	SceneDesc.gravity = U2PVector(FVector(0.0f, 0.0f, -9.81f));
	SceneDesc.cpuDispatcher = GPhysXCpuDispatcher;
	SceneDesc.filterShader = PhysXSimulationFilterShader;
	SceneDesc.simulationEventCallback = EventCallback;

	// Kinematic-Kinematic(캐릭터-트리거)과 Static-Kinematic 쌍에서도 이벤트를 받기 위해 eKEEP으로 켠다.
	SceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;
	SceneDesc.staticKineFilteringMode = PxPairFilteringMode::eKEEP;

	// 5단계에서 움직인 Dynamic 액터만 골라 동기화할 때 필요하다.
	SceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;
	// 안정적인 접촉 생성을 위해 PCM(Persistent Contact Manifold)을 켠다.
	SceneDesc.flags |= PxSceneFlag::eENABLE_PCM;

	PxSceneInstance = GPhysXSDK->createScene(SceneDesc);
	if (!PxSceneInstance)
	{
		HTR_LOG(Error, "[PhysX] GPhysXSDK->createScene failed");
		delete EventCallback;
		EventCallback = nullptr;
		return;
	}

	// PVD가 연결되어 있으면 제약·접촉·쿼리 정보를 함께 전송하도록 켠다.
	if (PxPvdSceneClient* PvdClient = PxSceneInstance->getScenePvdClient())
	{
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
	}
}

FPhysScene::~FPhysScene()
{
	if (PxSceneInstance)
	{
		PxSceneInstance->setSimulationEventCallback(nullptr);
		PxSceneInstance->release();
		PxSceneInstance = nullptr;
	}

	if (EventCallback)
	{
		delete EventCallback;
		EventCallback = nullptr;
	}
}

void FPhysScene::Tick(float DeltaTime)
{
	if (!PxSceneInstance || DeltaTime <= 0.0f)
	{
		return;
	}

	// 브레이크포인트나 프레임 드랍 직후 거대한 DeltaTime이 들어와도 폭주하지 않게 자른다.
	Accumulator += std::min(DeltaTime, MaxFrameDeltaTime);

	std::unordered_set<UPrimitiveComponent*> ComponentsToSync;

	int32 Steps = 0;
	while (Accumulator >= FixedDeltaTime && Steps < MaxSubSteps)
	{
		PxSceneInstance->simulate(FixedDeltaTime);
		PxSceneInstance->fetchResults(true);

		// 직전 스텝에서 움직인 Dynamic 액터를 모아 둔다.
		// (앞 스텝에서 움직였다가 다음 스텝에서 Sleep 상태가 된 액터도 놓치지 않기 위해 매 스텝 수집)
		PxU32 NumActiveActors = 0;
		PxActor** ActiveActors = PxSceneInstance->getActiveActors(NumActiveActors);
		for (PxU32 i = 0; i < NumActiveActors; ++i)
		{
			PxRigidDynamic* DynamicActor = ActiveActors[i]->is<PxRigidDynamic>();
			if (!DynamicActor)
			{
				continue;
			}
			if (DynamicActor->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
			{
				continue;
			}

			if (UPrimitiveComponent* Comp = static_cast<UPrimitiveComponent*>(DynamicActor->userData))
			{
				ComponentsToSync.insert(Comp);
			}
		}

		Accumulator -= FixedDeltaTime;
		++Steps;
	}

	// 최대 서브스텝을 다 돌고도 남은 시간은 버려 시뮬레이션이 영구히 뒤처지는 현상을 막는다.
	if (Steps == MaxSubSteps && Accumulator >= FixedDeltaTime)
	{
		Accumulator = 0.0f;
	}

	// 수집한 Dynamic 컴포넌트들의 최종 Transform과 속도를 프레임당 한 번만 엔진에 반영한다.
	for (UPrimitiveComponent* Comp : ComponentsToSync)
	{
		Comp->SyncComponentToRBPhysics();
	}

	// 수집한 Dynamic 컴포넌트들의 최종 Transform과 속도를 프레임당 한 번만 엔진에 반영한다.
	for (UPrimitiveComponent* Comp : ComponentsToSync)
	{
		Comp->SyncComponentToRBPhysics();
	}

	// fetchResults() 중에 콜백이 쌓아 둔 Overlap/Hit 이벤트를 안전하게 Broadcast한다.
	FlushSimulationEvents();
}

void FPhysScene::FlushSimulationEvents()
{
	if (!EventCallback)
	{
		return;
	}

	// Broadcast 도중 재진입에 대비해 큐를 로컬로 옮기고 먼저 비운다.
	std::vector<FPendingOverlapEvent> Overlaps;
	std::vector<FPendingHitEvent> Hits;
	Overlaps.swap(EventCallback->PendingOverlaps);
	Hits.swap(EventCallback->PendingHits);

	for (const FPendingOverlapEvent& Ev : Overlaps)
	{
		if (!Ev.CompA || !Ev.CompB || !Ev.CompA->IsRegistered() || !Ev.CompB->IsRegistered())
		{
			continue;
		}

		if (Ev.bBegin)
		{
			AActor* OwnerA = Ev.CompA->GetOwner();
			AActor* OwnerB = Ev.CompB->GetOwner();
			if (!OwnerA || !OwnerB || OwnerA == OwnerB)
			{
				continue;
			}
			if (!OwnerA->GetActorEnableCollision() || !OwnerB->GetActorEnableCollision())
			{
				continue;
			}
			if (!Ev.CompA->bGenerateOverlapEvents || !Ev.CompB->bGenerateOverlapEvents)
			{
				continue;
			}

			Ev.CompA->BeginComponentOverlap(Ev.CompB);
			if (Ev.CompA->IsRegistered() && Ev.CompB->IsRegistered())
			{
				Ev.CompB->BeginComponentOverlap(Ev.CompA);
			}
		}
		else
		{
			Ev.CompA->EndComponentOverlap(Ev.CompB);
			if (Ev.CompA->IsRegistered() && Ev.CompB->IsRegistered())
			{
				Ev.CompB->EndComponentOverlap(Ev.CompA);
			}
		}
	}

	for (const FPendingHitEvent& Ev : Hits)
	{
		if (!Ev.CompA || !Ev.CompB || !Ev.CompA->IsRegistered() || !Ev.CompB->IsRegistered())
		{
			continue;
		}

		AActor* OwnerA = Ev.CompA->GetOwner();
		AActor* OwnerB = Ev.CompB->GetOwner();
		if (!OwnerA || !OwnerB || OwnerA == OwnerB)
		{
			continue;
		}
		if (!OwnerA->GetActorEnableCollision() || !OwnerB->GetActorEnableCollision())
		{
			continue;
		}

		Ev.CompA->DispatchHit(Ev.CompB);
		if (Ev.CompA->IsRegistered() && Ev.CompB->IsRegistered())
		{
			Ev.CompB->DispatchHit(Ev.CompA);
		}
	}
}

bool FPhysScene::RaycastSingle(
	const FVector& Origin,
	const FVector& UnitDir,
	float MaxDistance,
	FHitResult& OutHit,
	AActor* IgnoreActor,
	bool bBlockingOnly) const
{
	OutHit = FHitResult(1.0f);
	OutHit.Init(Origin, Origin + UnitDir * MaxDistance);

	if (!PxSceneInstance || MaxDistance <= 0.0f)
	{
		return false;
	}

	PxRaycastBuffer HitBuffer;
	const PxHitFlags HitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL;
	const PxQueryFilterData FilterData(
		PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
	FPhysXQueryFilterCallback FilterCallback(OwningWorld, IgnoreActor, bBlockingOnly);

	if (!PxSceneInstance->raycast(
		U2PVector(Origin),
		U2PVector(UnitDir),
		MaxDistance,
		HitBuffer,
		HitFlags,
		FilterData,
		&FilterCallback) || !HitBuffer.hasBlock)
	{
		return false;
	}

	const PxRaycastHit& Hit = HitBuffer.block;
	const bool bInitialOverlap = Hit.hadInitialOverlap();

	OutHit.bBlockingHit = true;
	OutHit.bStartPentrating = bInitialOverlap;
	OutHit.Distance = bInitialOverlap ? 0.0f : Hit.distance;
	OutHit.Time = OutHit.Distance / MaxDistance;
	OutHit.PenetrationDepth = 0.0f;
	OutHit.Location = Origin + UnitDir * OutHit.Distance;
	OutHit.ImpactPoint = bInitialOverlap ? Origin : P2UVector(Hit.position);
	OutHit.Normal = bInitialOverlap ? -UnitDir : P2UVector(Hit.normal);
	OutHit.ImpactNormal = OutHit.Normal;
	OutHit.FaceIndex = (Hit.faceIndex == 0xFFFFffff) ? -1 : static_cast<int32>(Hit.faceIndex);
	OutHit.HitComponent = static_cast<UPrimitiveComponent*>(
		(Hit.shape && Hit.shape->userData) ? Hit.shape->userData
		: (Hit.actor ? Hit.actor->userData : nullptr));

	return true;
}

bool FPhysScene::SweepSphereSingle(
	const FVector& Origin,
	const FVector& UnitDir,
	float MaxDistance,
	float Radius,
	FHitResult& OutHit,
	AActor* IgnoreActor,
	bool bBlockingOnly) const
{
	OutHit = FHitResult(1.0f);
	OutHit.Init(Origin, Origin + UnitDir * MaxDistance);

	if (!PxSceneInstance || MaxDistance <= 0.0f || Radius <= 0.0f)
	{
		return false;
	}

	const PxSphereGeometry SphereGeom(Radius);
	const PxTransform StartPose(U2PVector(Origin));

	PxSweepBuffer HitBuffer;
	// eMTD: 시작 위치에서 이미 겹쳐 있을 때 최소 탈출 법선과 침투 깊이를 계산한다.
	const PxHitFlags HitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eMTD;
	const PxQueryFilterData FilterData(
		PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
	FPhysXQueryFilterCallback FilterCallback(OwningWorld, IgnoreActor, bBlockingOnly);

	if (!PxSceneInstance->sweep(
		SphereGeom,
		StartPose,
		U2PVector(UnitDir),
		MaxDistance,
		HitBuffer,
		HitFlags,
		FilterData,
		&FilterCallback) || !HitBuffer.hasBlock)
	{
		return false;
	}

	const PxSweepHit& Hit = HitBuffer.block;
	const bool bInitialOverlap = Hit.hadInitialOverlap();

	FVector Normal = P2UVector(Hit.normal);
	if (Normal.SizeSquared() <= SMALL_NUMBER)
	{
		Normal = -UnitDir;
	}

	OutHit.bBlockingHit = true;
	OutHit.bStartPentrating = bInitialOverlap;
	OutHit.Distance = bInitialOverlap ? 0.0f : Hit.distance;
	OutHit.Time = OutHit.Distance / MaxDistance;
	OutHit.PenetrationDepth = bInitialOverlap ? std::max(-Hit.distance, 0.0f) : 0.0f;
	OutHit.Location = Origin + UnitDir * OutHit.Distance;
	OutHit.Normal = Normal;
	OutHit.ImpactNormal = Normal;
	OutHit.ImpactPoint = (Hit.flags & PxHitFlag::ePOSITION)
		? P2UVector(Hit.position)
		: (OutHit.Location - Normal * Radius);
	OutHit.FaceIndex = (Hit.faceIndex == 0xFFFFffff) ? -1 : static_cast<int32>(Hit.faceIndex);
	OutHit.HitComponent = static_cast<UPrimitiveComponent*>(
		(Hit.shape && Hit.shape->userData) ? Hit.shape->userData
		: (Hit.actor ? Hit.actor->userData : nullptr));

	return true;
}

void FPhysScene::SetDebugVisualization(bool bEnable)
{
	if (!PxSceneInstance || bDebugVisualizationEnabled == bEnable)
	{
		return;
	}

	bDebugVisualizationEnabled = bEnable;

	if (bEnable)
	{
		// eSCALE은 축·조인트 제한 콘의 길이 스케일(m 단위)이며, > 0이어야 RenderBuffer가 채워진다.
		// 충돌 도형(eCOLLISION_SHAPES)은 eSCALE 크기와 무관하게 실제 도형 크기로 그려진다.
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eSCALE, 0.5f);
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, 1.0f);
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, 1.0f);
	}
	else
	{
		// eSCALE을 0으로 끄면 simulate() 중 디버그 선분 생성을 완전히 건너뛰어 오버헤드가 0이 된다.
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eSCALE, 0.0f);
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 0.0f);
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, 0.0f);
		PxSceneInstance->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, 0.0f);
	}
}

void FPhysScene::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!PxSceneInstance || !LineBatcher || !bDebugVisualizationEnabled)
	{
		return;
	}

	const PxRenderBuffer& RenderBuffer = PxSceneInstance->getRenderBuffer();
	const PxU32 NumLines = RenderBuffer.getNbLines();
	const PxDebugLine* Lines = RenderBuffer.getLines();

	for (PxU32 i = 0; i < NumLines; ++i)
	{
		// PhysX PxDebugColor는 0x00RRGGBB 형식
		const PxU32 RawColor = Lines[i].color0;
		const FVector4 Color(
			((RawColor >> 16) & 0xFF) / 255.0f,
			((RawColor >> 8) & 0xFF) / 255.0f,
			(RawColor & 0xFF) / 255.0f,
			1.0f);

		LineBatcher->AddLine(P2UVector(Lines[i].pos0), P2UVector(Lines[i].pos1), Color);
	}
}