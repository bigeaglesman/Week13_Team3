#include "EnginePCH.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXConversion.h"
#include "Physics/BodyInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Render/LineBatcher.h"
#include "Core/Stats/LightweightStats.h"

#include <PxPhysicsAPI.h>
#include "Physics/PhysXSceneLock.h"

using namespace physx;

DECLARE_CYCLE_STAT("Physics: Simulate", STAT_PhysicsSimulate);
DECLARE_CYCLE_STAT("Physics: FetchResults", STAT_PhysicsFetchResults);
DECLARE_CYCLE_STAT("Physics: Sync Bodies", STAT_PhysicsSyncBodies);
DECLARE_CYCLE_STAT("Physics: Dispatch Notifies", STAT_PhysicsDispatchNotifies);

namespace
{
	// UE의 PhysXSimFilterShader 자리다.
	//   word0: 0이 아니고 같으면 서로 충돌하지 않는다 (래그돌 뼈끼리)
	//   word1: FBodyInstance::FilterFlag_NotifyHit이 한쪽이라도 있으면 처음 닿을 때 onContact를 받는다
	// 겹침(Overlap) 이벤트는 엔진의 기존 Overlap 시스템(UWorld::UpdateOverlaps)이 맡는다.
	PxFilterFlags EngineFilterShader(
		PxFilterObjectAttributes, PxFilterData Data0,
		PxFilterObjectAttributes, PxFilterData Data1,
		PxPairFlags& PairFlags, const void*, PxU32)
	{
		if (Data0.word0 != 0 && Data0.word0 == Data1.word0)
		{
			return PxFilterFlag::eSUPPRESS;
		}

		PairFlags = PxPairFlag::eCONTACT_DEFAULT;

		if ((Data0.word1 | Data1.word1) & FBodyInstance::FilterFlag_NotifyHit)
		{
			PairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_CONTACT_POINTS;
		}
		return PxFilterFlag::eDEFAULT;
	}

	// PhysX 디버그 색(0xAARRGGBB) → 선 색
	FVector4 ToLineColor(PxU32 Color)
	{
		return FVector4(((Color >> 16) & 0xFF) / 255.0f, ((Color >> 8) & 0xFF) / 255.0f, (Color & 0xFF) / 255.0f, 1.0f);
	}
}

// PhysX가 fetchResults 안(게임 스레드, 씬 쓰기 락을 잡은 상태)에서 부르는 콜백.
// 여기서는 엔진 코드를 부르지 않고 FPhysicsScene에 모아 두기만 한다. UE의 FPhysXSimEventCallback 자리다.
class FPhysicsSimulationEventCallback : public PxSimulationEventCallback
{
public:
	explicit FPhysicsSimulationEventCallback(FPhysicsScene& InOwner) : Owner(InOwner) {}

	void onContact(const PxContactPairHeader& PairHeader, const PxContactPair* Pairs, PxU32 NbPairs) override
	{
		// 이번 스텝에 지워진 액터는 userData를 믿을 수 없다.
		if ((PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_0) ||
			(PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
		{
			return;
		}

		FBodyInstance* BodyA = static_cast<FBodyInstance*>(PairHeader.actors[0]->userData);
		FBodyInstance* BodyB = static_cast<FBodyInstance*>(PairHeader.actors[1]->userData);
		if (!BodyA || !BodyB)
		{
			return;
		}

		for (PxU32 i = 0; i < NbPairs; ++i)
		{
			const PxContactPair& Pair = Pairs[i];
			if (!(Pair.events & PxPairFlag::eNOTIFY_TOUCH_FOUND))
			{
				continue;
			}
			if ((Pair.flags & PxContactPairFlag::eREMOVED_SHAPE_0) || (Pair.flags & PxContactPairFlag::eREMOVED_SHAPE_1))
			{
				continue;
			}

			FCollisionNotifyInfo Info;
			Info.BodyA = BodyA;
			Info.BodyB = BodyB;
			Info.bNotifyA = BodyA->bNotifyRigidBodyCollision;
			Info.bNotifyB = BodyB->bNotifyRigidBodyCollision;

			// 접점 하나만 가져온다. PhysX 노멀은 두 번째 도형(B)에서 첫 번째(A)를 향한다.
			PxContactPairPoint Point;
			if (Pair.contactCount > 0 && Pair.extractContacts(&Point, 1) > 0)
			{
				Info.ImpactPoint = ToFVector(Point.position);
				Info.ImpactNormal = ToFVector(Point.normal);
			}

			Owner.AddPendingCollisionNotify(Info);
			break;   // 액터 쌍 하나당 이벤트 하나 (도형이 여러 개여도)
		}
	}

	void onTrigger(PxTriggerPair*, PxU32) override {}
	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

private:
	FPhysicsScene& Owner;
};

FPhysicsScene::FPhysicsScene(UWorld* InOwner)
	: Owner(InOwner)
{
	FPhysicsCore& Core = FPhysicsCore::Get();
	if (!Core.IsInitialized())
	{
		return;
	}

	PxPhysics* Physics = Core.GetPhysics();
	EventCallback = new FPhysicsSimulationEventCallback(*this);

	PxSceneDesc Desc(Physics->getTolerancesScale());
	Desc.gravity       = PxVec3(0.0f, 0.0f, -9.81f);   // Z-up, m/s²
	Desc.cpuDispatcher = Core.GetDispatcher();          // simulate를 나눠 돌릴 작업 스레드
	Desc.filterShader  = EngineFilterShader;
	Desc.simulationEventCallback = EventCallback;
	Desc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;        // 움직인 액터만 동기화
	Desc.flags |= PxSceneFlag::eENABLE_CCD;
	Desc.flags |= PxSceneFlag::eENABLE_PCM;
	Desc.flags |= PxSceneFlag::eREQUIRE_RW_LOCK;             // 씬 API 호출마다 읽기·쓰기 락을 요구한다 (빠뜨리면 PhysX가 경고)

	Scene = Physics->createScene(Desc);
	if (Scene)
	{
		// PxScene → FPhysicsScene 역참조. 바디가 지워질 때 이걸로 대기 중인 이벤트를 정리한다.
		Scene->userData = this;
	}
}

FPhysicsScene::~FPhysicsScene()
{
	if (Scene)
	{
		Scene->userData = nullptr;
		Scene->release();
		Scene = nullptr;
	}
	delete EventCallback;
	EventCallback = nullptr;
}

void FPhysicsScene::Tick(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f)
	{
		return;
	}

	// 지난 프레임에 그리겠다고 요청한 뷰포트가 있으면 이번 스텝부터 PhysX가 시각화 선을 채운다.
	SetDebugVisualization(RequestedVisualization);
	RequestedVisualization = EDebugVisualization::Off;

	Accumulator += DeltaTime;

	int Steps = 0;
	while (Accumulator >= FixedStep && Steps < MaxSubSteps)
	{
		StepOnce(FixedStep);
		Accumulator -= FixedStep;
		++Steps;
	}

	// 한 프레임에 MaxSubSteps를 다 써도 밀려 있으면 버린다.
	// 따라잡으려고 더 돌면 프레임이 더 느려지고 다음엔 더 밀리는 악순환이 된다.
	if (Accumulator >= FixedStep)
	{
		Accumulator = 0.0f;
	}

	if (Steps > 0)
	{
		SyncActiveBodiesToComponents();
		DispatchPhysNotifications();
	}
}

void FPhysicsScene::SyncActiveBodiesToComponents()
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsSyncBodies);

	// 마지막 simulate에서 움직인 액터만 온다 (eENABLE_ACTIVE_ACTORS). 잠든 바디와 바닥은 빠진다.
	// 목록만 읽기 락 안에서 뽑는다. 컴포넌트를 옮기면 자식 키네마틱 바디가 쓰기 락을 잡는데,
	// 읽기 락을 쥔 채로 쓰기 락을 잡으면(락 승격) PhysX가 막는다.
	TArray<FBodyInstance*> MovedBodies;
	{
		SCOPED_SCENE_READ_LOCK(Scene);
		PxU32 NumActive = 0;
		PxActor** ActiveActors = Scene->getActiveActors(NumActive);
		for (PxU32 i = 0; i < NumActive; ++i)
		{
			// 뼈 바디는 건너뛴다. 뼈 결과는 스켈레탈 메시 컴포넌트가 물리 이후에 한꺼번에 가져간다 (UE의 BlendPhysicsBones).
			FBodyInstance* Body = static_cast<FBodyInstance*>(ActiveActors[i]->userData);
			if (Body && Body->InstanceBoneIndex < 0 && Body->GetOwnerComponent())
			{
				MovedBodies.Add(Body);
			}
		}
	}

	for (FBodyInstance* Body : MovedBodies)
	{
		Body->GetOwnerComponent()->SyncComponentToRBPhysics();
	}
}

void FPhysicsScene::StepOnce(float Step)
{
	// simulate ~ fetchResults 동안 엔진 쪽에서 씬을 건드리지 않도록 쓰기 락. onContact도 이 안에서 불린다.
	SCOPED_SCENE_WRITE_LOCK(Scene);
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysicsSimulate);
		Scene->simulate(Step);
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysicsFetchResults);
		Scene->fetchResults(true);
	}
}

void FPhysicsScene::AddPendingCollisionNotify(const FCollisionNotifyInfo& Info)
{
	PendingCollisionNotifies.Add(Info);
}

void FPhysicsScene::RemovePendingNotifies(const FBodyInstance* Body)
{
	// 지우지 않고 비워 둔다. 보내는 도중(DispatchPhysNotifications)에 불려도 인덱스가 밀리지 않는다.
	for (FCollisionNotifyInfo& Info : PendingCollisionNotifies)
	{
		if (Info.BodyA == Body) Info.BodyA = nullptr;
		if (Info.BodyB == Body) Info.BodyB = nullptr;
	}
}

void FPhysicsScene::DispatchPhysNotifications()
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsDispatchNotifies);

	// 받는 쪽(OnComponentHit, Lua 등)이 액터를 지우면 RemovePendingNotifies가 남은 항목을 비운다.
	// 보내는 동안엔 새 항목이 추가되지 않으므로(simulate 밖) 배열이 다시 할당되지 않는다.
	for (int32 i = 0; i < PendingCollisionNotifies.Num(); ++i)
	{
		const FCollisionNotifyInfo& Info = PendingCollisionNotifies[i];

		if (Info.bNotifyA && Info.BodyA && Info.BodyB)
		{
			UPrimitiveComponent* CompA = Info.BodyA->GetOwnerComponent();
			UPrimitiveComponent* CompB = Info.BodyB->GetOwnerComponent();
			if (CompA && CompB && CompA != CompB)
			{
				CompA->DispatchHit(CompB);
			}
		}

		// A 쪽 이벤트에서 B가 지워졌을 수 있으니 다시 확인한다.
		if (Info.bNotifyB && Info.BodyA && Info.BodyB)
		{
			UPrimitiveComponent* CompA = Info.BodyA->GetOwnerComponent();
			UPrimitiveComponent* CompB = Info.BodyB->GetOwnerComponent();
			if (CompA && CompB && CompA != CompB)
			{
				CompB->DispatchHit(CompA);
			}
		}
	}
	PendingCollisionNotifies.Empty();
}

void FPhysicsScene::SetDebugVisualization(EDebugVisualization Mode)
{
	if (!Scene || DebugVisualization == Mode)
	{
		return;
	}
	DebugVisualization = Mode;

	// eSCALE이 0이면 PhysX가 렌더 버퍼를 아예 채우지 않는다. 꺼 두면 비용이 없다.
	const float On = Mode != EDebugVisualization::Off ? 1.0f : 0.0f;
	const float Joints = Mode == EDebugVisualization::ShapesAndJoints ? 1.0f : 0.0f;
	SCOPED_SCENE_WRITE_LOCK(Scene);
	Scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, Joints);
	Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, Joints);
}

void FPhysicsScene::DrawDebug(FLineBatcher& LineBatcher) const
{
	if (!Scene || DebugVisualization == EDebugVisualization::Off)
	{
		return;
	}

	// 마지막 fetchResults 시점의 도형·조인트 선
	SCOPED_SCENE_READ_LOCK(Scene);
	const PxRenderBuffer& Buffer = Scene->getRenderBuffer();

	const PxDebugLine* Lines = Buffer.getLines();
	for (PxU32 i = 0; i < Buffer.getNbLines(); ++i)
	{
		LineBatcher.AddLine(ToFVector(Lines[i].pos0), ToFVector(Lines[i].pos1), ToLineColor(Lines[i].color0));
	}

	const PxDebugTriangle* Tris = Buffer.getTriangles();
	for (PxU32 i = 0; i < Buffer.getNbTriangles(); ++i)
	{
		const FVector A = ToFVector(Tris[i].pos0);
		const FVector B = ToFVector(Tris[i].pos1);
		const FVector C = ToFVector(Tris[i].pos2);
		const FVector4 Color = ToLineColor(Tris[i].color0);
		LineBatcher.AddLine(A, B, Color);
		LineBatcher.AddLine(B, C, Color);
		LineBatcher.AddLine(C, A, Color);
	}
}
