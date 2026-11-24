#include "EnginePCH.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXConversion.h"
#include "Physics/BodyInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Render/LineBatcher.h"
#include "Core/Stats/LightweightStats.h"
#include "Physics/PhysicalMaterial.h"
#include "UObject/UObjectIterator.h"

#include <PxPhysicsAPI.h>
#include <PxBatchQuery.h>
#include <PxBatchQueryDesc.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <vehicle/PxVehicleUtil.h>

#include "Physics/PhysXSceneLock.h"

using namespace physx;

namespace
{
	PxQueryHitType::Enum VehicleSuspensionPreFilter(PxFilterData QueryFilterData, PxFilterData ObjectFilterData, const void*, PxU32, PxHitFlags&)
	{
		return (QueryFilterData.word0 & ObjectFilterData.word0) != 0 ? PxQueryHitType::eBLOCK : PxQueryHitType::eNONE;
	}
}

struct FVehicleSceneQueryData
{
	PxBatchQuery* BatchQuery = nullptr;
	PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;

	std::vector<PxVehicleWheels*> Vehicles;
	std::vector<USceneComponent*> SyncComponents;
	std::vector<PxRaycastQueryResult> RaycastResults;
	std::vector<PxVehicleDrive4WRawInputData> RawInputs;
	std::vector<std::vector<PxWheelQueryResult>> WheelQueryResults;
	std::vector<PxVehicleWheelQueryResult> VehicleWheelQueryResults;

	PxU32 TotalWheelCount = 0;
	PxU32 MaxWheelCount = 0;

	~FVehicleSceneQueryData()
	{
		if (FrictionPairs)
		{
			FrictionPairs->release();
			FrictionPairs = nullptr;
		}
	}

	bool Initialize(PxMaterial* DefaultMaterial)
	{
		if (!DefaultMaterial)
		{
			return false;
		}

		FrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, PxVehicleDrivableSurfaceToTireFrictionPairs::eMAX_NB_SURFACE_TYPES);
		if (!FrictionPairs)
		{
			return false;
		}

		std::vector<const PxMaterial*> Materials;
		std::vector<PxVehicleDrivableSurfaceType> SurfaceTypes;

		Materials.push_back(DefaultMaterial);

		PxVehicleDrivableSurfaceType DefaultSurfaceType;
		DefaultSurfaceType.mType = static_cast<PxU32>(EVehicleSurfaceType::Asphalt);
		SurfaceTypes.push_back(DefaultSurfaceType);

		for (TObjectIterator<UPhysicalMaterial> It; It; ++It)
		{
			UPhysicalMaterial* PhysicalMaterial = *It;
			if (!PhysicalMaterial)
			{
				continue;
			}

			PxMaterial* Material = PhysicalMaterial->GetPxMaterial();
			if (!Material)
			{
				continue;
			}

			const PxU32 SurfaceType = static_cast<PxU32>(PhysicalMaterial->VehicleSurfaceType);
			if (SurfaceType > static_cast<PxU32>(EVehicleSurfaceType::Metal))
			{
				continue;
			}

			if (std::find(Materials.begin(), Materials.end(), Material) != Materials.end())
			{
				continue;
			}

			if (Materials.size() >= PxVehicleDrivableSurfaceToTireFrictionPairs::eMAX_NB_SURFACE_TYPES)
			{
				HTR_LOG(Warning, "[Vehicle] Maximum drivable surface material count reached.");
				break;
			}

			Materials.push_back(Material);

			PxVehicleDrivableSurfaceType SurfaceTypeData;
			SurfaceTypeData.mType = SurfaceType;
			SurfaceTypes.push_back(SurfaceTypeData);
		}

		FrictionPairs->setup(1, static_cast<PxU32>(Materials.size()), Materials.data(), SurfaceTypes.data());

		FrictionPairs->setTypePairFriction(static_cast<PxU32>(EVehicleSurfaceType::Asphalt), 0, 1.0f);
		FrictionPairs->setTypePairFriction(static_cast<PxU32>(EVehicleSurfaceType::Dirt), 0, 0.72f);
		FrictionPairs->setTypePairFriction(static_cast<PxU32>(EVehicleSurfaceType::Ice), 0, 0.15f);
		FrictionPairs->setTypePairFriction(static_cast<PxU32>(EVehicleSurfaceType::Grass), 0, 0.55f);
		FrictionPairs->setTypePairFriction(static_cast<PxU32>(EVehicleSurfaceType::Metal), 0, 0.85f);

		return true;
	}

	bool EnsureCapacity(PxScene* Scene, PxU32 RequiredWheelCount)
	{
		if (!Scene || RequiredWheelCount == 0)
		{
			return false;
		}

		if (BatchQuery && RequiredWheelCount <= MaxWheelCount)
		{
			return true;
		}

		const PxU32 NewCapacity = std::max(RequiredWheelCount, MaxWheelCount > 0 ? MaxWheelCount * 2 : RequiredWheelCount);
		std::vector<PxRaycastQueryResult> NewRaycastResults(NewCapacity);

		PxBatchQueryDesc QueryDesc(NewCapacity, 0, 0);
		QueryDesc.queryMemory.userRaycastResultBuffer = NewRaycastResults.data();
		QueryDesc.preFilterShader = VehicleSuspensionPreFilter;

		PxBatchQuery* NewBatchQuery = Scene->createBatchQuery(QueryDesc);
		if (!NewBatchQuery)
		{
			return false;
		}

		if (BatchQuery)
		{
			BatchQuery->release();
		}

		BatchQuery = NewBatchQuery;
		RaycastResults.swap(NewRaycastResults);
		MaxWheelCount = NewCapacity;

		return true;
	}
};

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

		UPrimitiveComponent* CompA = BodyA ? BodyA->GetOwnerComponent() : Owner.FindVehicleComponent(PairHeader.actors[0]);
		UPrimitiveComponent* CompB = BodyB ? BodyB->GetOwnerComponent() : Owner.FindVehicleComponent(PairHeader.actors[1]);

		if (!CompA || !CompB)
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
			Info.ComponentA = CompA;
			Info.ComponentB = CompB;
			Info.bNotifyA = BodyA ? BodyA->bNotifyRigidBodyCollision : CompA->BodyInstance.bNotifyRigidBodyCollision;
			Info.bNotifyB = BodyB ? BodyB->bNotifyRigidBodyCollision : CompB->BodyInstance.bNotifyRigidBodyCollision;

			// 접점 하나만 가져온다. PhysX 노멀은 두 번째 도형(B)에서 첫 번째(A)를 향한다.
			PxContactPairPoint Point;
			if (Pair.contactCount > 0 && Pair.extractContacts(&Point, 1) > 0)
			{
				Info.ImpactPoint = ToFVector(Point.position);
				Info.ImpactNormal = ToFVector(Point.normal);
				Info.NormalImpulse = ToFVector(Point.impulse);
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
		{
			SCOPED_SCENE_WRITE_LOCK(Scene);

			if (VehicleSceneQueryData && VehicleSceneQueryData->BatchQuery)
			{
				VehicleSceneQueryData->BatchQuery->release();
				VehicleSceneQueryData->BatchQuery = nullptr;
			}

			Scene->userData = nullptr;
		}

		Scene->release();
		Scene = nullptr;
	}

	delete VehicleSceneQueryData;
	VehicleSceneQueryData = nullptr;

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
	SetDebugVisualization(bDebugVisualizationRequested);
	bDebugVisualizationRequested = false;

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
		SyncVehiclesToComponents();
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

void FPhysicsScene::SyncVehiclesToComponents()
{
	if (!Scene || !VehicleSceneQueryData)
	{
		return;
	}

	struct FVehiclePoseSync
	{
		USceneComponent* Component = nullptr;
		PxTransform Pose;
	};

	const FVehicleSceneQueryData& QueryData = *VehicleSceneQueryData;
	std::vector<FVehiclePoseSync> SyncData;

	{
		SCOPED_SCENE_READ_LOCK(Scene);

		const size_t Count = std::min(QueryData.Vehicles.size(), QueryData.SyncComponents.size());

		for (size_t i = 0; i < Count; ++i)
		{
			PxVehicleWheels* Vehicle = QueryData.Vehicles[i];
			USceneComponent* Component = QueryData.SyncComponents[i];

			if (!Vehicle || !Component)
			{
				continue;
			}

			PxRigidDynamic* ChassisActor = Vehicle->getRigidDynamicActor();

			if (!ChassisActor)
			{
				continue;
			}

			SyncData.push_back({ Component, ChassisActor->getGlobalPose() });
		}
	}

	for (const FVehiclePoseSync& Data : SyncData)
	{
		Data.Component->SetWorldLocationAndRotation(ToFVector(Data.Pose.p), ToFQuat(Data.Pose.q).ToFRotator(), false);
	}
}

void FPhysicsScene::StepOnce(float Step)
{
	// simulate ~ fetchResults 동안 엔진 쪽에서 씬을 건드리지 않도록 쓰기 락. onContact도 이 안에서 불린다.
	SCOPED_SCENE_WRITE_LOCK(Scene);

	if (VehicleSceneQueryData && !VehicleSceneQueryData->Vehicles.empty())
	{
		FVehicleSceneQueryData& QueryData = *VehicleSceneQueryData;
		const PxU32 VehicleCount = static_cast<PxU32>(QueryData.Vehicles.size());

		static const PxVehicleKeySmoothingData KeySmoothingData =
		{
			{ 7.0f, 8.0f, 2.5f, 5.0f, 5.0f },
			{ 9.0f, 10.0f, 8.0f, 7.0f, 7.0f }
		};

		static const PxFixedSizeLookupTable<8> SteerVsForwardSpeedTable = []()
			{
				PxFixedSizeLookupTable<8> Table;
				Table.addPair(0.0f, 0.9f);
				Table.addPair(5.0f, 0.9f);
				Table.addPair(30.0f, 0.48f);
				Table.addPair(120.0f, 0.28f);
				return Table;
			}();

		for (PxU32 i = 0; i < VehicleCount; ++i)
		{
			PxVehicleDrive4W* DriveVehicle = static_cast<PxVehicleDrive4W*>(QueryData.Vehicles[i]);

			PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(KeySmoothingData, SteerVsForwardSpeedTable, QueryData.RawInputs[i], Step, false, *DriveVehicle);
		}

		PxVehicleSuspensionRaycasts(QueryData.BatchQuery, VehicleCount, QueryData.Vehicles.data(), QueryData.TotalWheelCount, QueryData.RaycastResults.data());

		PxVehicleUpdates(Step, Scene->getGravity(), *QueryData.FrictionPairs, VehicleCount, QueryData.Vehicles.data(), QueryData.VehicleWheelQueryResults.data());
	}

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
		if (Info.BodyA == Body)
		{
			Info.BodyA = nullptr;
			Info.ComponentA = nullptr;
			Info.bNotifyA = false;
		}

		if (Info.BodyB == Body)
		{
			Info.BodyB = nullptr;
			Info.ComponentB = nullptr;
			Info.bNotifyB = false;
		}
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

		if (Info.bNotifyA && Info.ComponentA && Info.ComponentB && Info.ComponentA != Info.ComponentB)
		{
			FHitResult Hit;
			Hit.HitComponent = Info.ComponentA;
			Hit.Location = Info.ImpactPoint;
			Hit.ImpactPoint = Info.ImpactPoint;
			Hit.Normal = Info.ImpactNormal;
			Hit.ImpactNormal = Info.ImpactNormal;
			Hit.NormalImpulse = Info.NormalImpulse;
			Hit.bBlockingHit = true;

			Info.ComponentA->DispatchHit(Info.ComponentB, Hit);
		}

		// A 쪽 이벤트에서 B가 지워졌을 수 있으니 다시 확인한다.
		if (Info.bNotifyB && Info.ComponentA && Info.ComponentB && Info.ComponentA != Info.ComponentB)
		{
			FHitResult Hit;
			Hit.HitComponent = Info.ComponentB;
			Hit.Location = Info.ImpactPoint;
			Hit.ImpactPoint = Info.ImpactPoint;
			Hit.Normal = -Info.ImpactNormal;
			Hit.ImpactNormal = -Info.ImpactNormal;
			Hit.NormalImpulse = -Info.NormalImpulse;
			Hit.bBlockingHit = true;

			Info.ComponentB->DispatchHit(Info.ComponentA, Hit);
		}
	}
	PendingCollisionNotifies.Empty();
}

bool FPhysicsScene::AddVehicle(PxVehicleWheels* Vehicle, USceneComponent* SyncComponent)
{
	if (!Scene || !Vehicle || !SyncComponent)
	{
		return false;
	}

	SCOPED_SCENE_WRITE_LOCK(Scene);

	if (!VehicleSceneQueryData)
	{
		VehicleSceneQueryData = new FVehicleSceneQueryData();

		if (!VehicleSceneQueryData->Initialize(FPhysicsCore::Get().GetDefaultMaterial()))
		{
			delete VehicleSceneQueryData;
			VehicleSceneQueryData = nullptr;
			return false;
		}
	}

	auto& QueryData = *VehicleSceneQueryData;

	auto ExistingIt = std::find(QueryData.Vehicles.begin(), QueryData.Vehicles.end(), Vehicle);

	if (ExistingIt != QueryData.Vehicles.end())
	{
		const size_t ExistingIndex = static_cast<size_t>(ExistingIt - QueryData.Vehicles.begin());
		QueryData.SyncComponents[ExistingIndex] = SyncComponent;
		return true;
	}

	const PxU32 WheelCount = Vehicle->mWheelsSimData.getNbWheels();
	if (WheelCount == 0 || !QueryData.EnsureCapacity(Scene, QueryData.TotalWheelCount + WheelCount))
	{
		return false;
	}

	QueryData.Vehicles.push_back(Vehicle);
	QueryData.SyncComponents.push_back(SyncComponent);
	QueryData.RawInputs.emplace_back();

	QueryData.WheelQueryResults.emplace_back(WheelCount);

	PxVehicleWheelQueryResult& WheelQuery = QueryData.VehicleWheelQueryResults.emplace_back();
	WheelQuery.wheelQueryResults = QueryData.WheelQueryResults.back().data();
	WheelQuery.nbWheelQueryResults = WheelCount;

	QueryData.TotalWheelCount += WheelCount;

	return true;
}

void FPhysicsScene::RemoveVehicle(PxVehicleWheels* Vehicle)
{
	if (!Scene || !Vehicle || !VehicleSceneQueryData)
	{
		return;
	}

	SCOPED_SCENE_WRITE_LOCK(Scene);

	auto& QueryData = *VehicleSceneQueryData;
	auto It = std::find(QueryData.Vehicles.begin(), QueryData.Vehicles.end(), Vehicle);

	if (It == QueryData.Vehicles.end())
	{
		return;
	}

	const size_t VehicleIndex = static_cast<size_t>(It - QueryData.Vehicles.begin());

	UPrimitiveComponent* RemovedComponent = Cast<UPrimitiveComponent>(QueryData.SyncComponents[VehicleIndex]);

	for (FCollisionNotifyInfo& Info : PendingCollisionNotifies)
	{
		if (Info.ComponentA == RemovedComponent)
		{
			Info.ComponentA = nullptr;
			Info.bNotifyA = false;
		}

		if (Info.ComponentB == RemovedComponent)
		{
			Info.ComponentB = nullptr;
			Info.bNotifyB = false;
		}
	}

	QueryData.RawInputs.erase(QueryData.RawInputs.begin() + VehicleIndex);
	QueryData.SyncComponents.erase(QueryData.SyncComponents.begin() + VehicleIndex);
	QueryData.WheelQueryResults.erase(QueryData.WheelQueryResults.begin() + VehicleIndex);
	QueryData.VehicleWheelQueryResults.erase(QueryData.VehicleWheelQueryResults.begin() + VehicleIndex);

	for (size_t i = 0; i < QueryData.VehicleWheelQueryResults.size(); ++i)
	{
		QueryData.VehicleWheelQueryResults[i].wheelQueryResults = QueryData.WheelQueryResults[i].data();
		QueryData.VehicleWheelQueryResults[i].nbWheelQueryResults = static_cast<PxU32>(QueryData.WheelQueryResults[i].size());
	}

	QueryData.TotalWheelCount -= Vehicle->mWheelsSimData.getNbWheels();
	QueryData.Vehicles.erase(It);
}

UPrimitiveComponent* FPhysicsScene::FindVehicleComponent(const PxRigidActor* Actor) const
{
	if (!VehicleSceneQueryData || !Actor)
	{
		return nullptr;
	}

	const auto& QueryData = *VehicleSceneQueryData;

	for (size_t i = 0; i < QueryData.Vehicles.size(); ++i)
	{
		if (QueryData.Vehicles[i] && QueryData.Vehicles[i]->getRigidDynamicActor() == Actor)
		{
			return Cast<UPrimitiveComponent>(QueryData.SyncComponents[i]);
		}
	}

	return nullptr;
}

void FPhysicsScene::SetVehicleInput(PxVehicleWheels* Vehicle, bool bAccelerate, bool bBrake, bool bHandbrake, bool bSteerLeft, bool bSteerRight)
{
	if (!Vehicle || !VehicleSceneQueryData)
	{
		return;
	}

	auto& QueryData = *VehicleSceneQueryData;
	auto It = std::find(QueryData.Vehicles.begin(), QueryData.Vehicles.end(), Vehicle);

	if (It == QueryData.Vehicles.end())
	{
		return;
	}

	const size_t VehicleIndex = static_cast<size_t>(It - QueryData.Vehicles.begin());
	PxVehicleDrive4WRawInputData& RawInput = QueryData.RawInputs[VehicleIndex];

	RawInput.setDigitalAccel(bAccelerate);
	RawInput.setDigitalBrake(bBrake);
	RawInput.setDigitalHandbrake(bHandbrake);
	RawInput.setDigitalSteerLeft(bSteerLeft);
	RawInput.setDigitalSteerRight(bSteerRight);
}

bool FPhysicsScene::GetVehicleWheelStates(physx::PxVehicleWheels* Vehicle, FVehicleWheelState* OutWheelStates, int32 MaxWheelCount, bool& OutIsInAir) const
{
	OutIsInAir = true;

	if (!Scene || !Vehicle || !VehicleSceneQueryData || !OutWheelStates || MaxWheelCount <= 0)
	{
		return false;
	}

	SCOPED_SCENE_READ_LOCK(Scene);

	const FVehicleSceneQueryData& QueryData = *VehicleSceneQueryData;
	auto It = std::find(QueryData.Vehicles.begin(), QueryData.Vehicles.end(), Vehicle);

	if (It == QueryData.Vehicles.end())
	{
		return false;
	}

	const size_t VehicleIndex = static_cast<size_t>(It - QueryData.Vehicles.begin());

	if (VehicleIndex >= QueryData.VehicleWheelQueryResults.size())
	{
		return false;
	}

	const PxU32 WheelCount = Vehicle->mWheelsSimData.getNbWheels();

	if (MaxWheelCount < static_cast<int32>(WheelCount))
	{
		return false;
	}

	const PxVehicleWheelQueryResult& VehicleResults = QueryData.VehicleWheelQueryResults[VehicleIndex];

	if (!VehicleResults.wheelQueryResults || VehicleResults.nbWheelQueryResults < WheelCount)
	{
		return false;
	}

	OutIsInAir = PxVehicleIsInAir(VehicleResults);

	for (PxU32 i = 0; i < WheelCount; ++i)
	{
		const PxWheelQueryResult& WheelResult = VehicleResults.wheelQueryResults[i];
		const PxVehicleWheelData& WheelData = Vehicle->mWheelsSimData.getWheelData(i);

		FVehicleWheelState& State = OutWheelStates[i];

		State.bIsInAir = WheelResult.isInAir;
		State.bHasContact = !WheelResult.isInAir && WheelResult.tireContactShape != nullptr;

		State.SuspensionLineStart = ToFVector(WheelResult.suspLineStart);
		State.SuspensionLineDirection = ToFVector(WheelResult.suspLineDir);
		State.SuspensionLineLength = WheelResult.suspLineLength;

		State.WheelCenterOffset = ToFVector(Vehicle->mWheelsSimData.getWheelCentreOffset(i));
		State.SuspensionTravelDirection = ToFVector(Vehicle->mWheelsSimData.getSuspTravelDirection(i));

		State.TireContactPoint = ToFVector(WheelResult.tireContactPoint);
		State.TireContactNormal = ToFVector(WheelResult.tireContactNormal);
		State.TireLongitudinalDirection = ToFVector(WheelResult.tireLongitudinalDir);
		State.TireLateralDirection = ToFVector(WheelResult.tireLateralDir);

		State.WheelRadius = WheelData.mRadius;
		State.WheelWidth = WheelData.mWidth;

		State.SuspensionJounce = WheelResult.suspJounce;
		State.SuspensionSpringForce = WheelResult.suspSpringForce;

		State.TireFriction = WheelResult.tireFriction;
		State.LongitudinalSlip = WheelResult.longitudinalSlip;
		State.LateralSlip = WheelResult.lateralSlip;

		State.SteerAngle = WheelResult.steerAngle;
		State.WheelRotationAngle = Vehicle->mWheelsDynData.getWheelRotationAngle(i);

		State.TireSurfaceType = static_cast<int32>(WheelResult.tireSurfaceType);
	}

	return true;
}


bool FPhysicsScene::GetVehicleDriveState(PxVehicleWheels* Vehicle, FVehicleDriveState& OutState) const
{
	if (!Scene || !Vehicle || !VehicleSceneQueryData)
	{
		return false;
	}

	SCOPED_SCENE_READ_LOCK(Scene);

	const auto& Vehicles = VehicleSceneQueryData->Vehicles;

	if (std::find(Vehicles.begin(), Vehicles.end(), Vehicle) == Vehicles.end())
	{
		return false;
	}

	const PxVehicleDrive4W* DriveVehicle = static_cast<const PxVehicleDrive4W*>(Vehicle);

	OutState.ForwardSpeed = Vehicle->computeForwardSpeed();
	OutState.EngineRPM = DriveVehicle->mDriveDynData.getEngineRotationSpeed() * 60.0f / (2.0f * 3.1415926535f);
	OutState.CurrentGear = static_cast<int32>(DriveVehicle->mDriveDynData.getCurrentGear());
	OutState.TargetGear = static_cast<int32>(DriveVehicle->mDriveDynData.getTargetGear());
	OutState.bUseAutoGears = DriveVehicle->mDriveDynData.getUseAutoGears();

	return true;
}

bool FPhysicsScene::SetVehicleGear(PxVehicleWheels* Vehicle, int32 Gear, bool bUseAutoGears, bool bForce)
{
	if (!Scene || !Vehicle || !VehicleSceneQueryData || Gear < PxVehicleGearsData::eREVERSE || Gear > PxVehicleGearsData::eFIFTH)
	{
		return false;
	}

	SCOPED_SCENE_WRITE_LOCK(Scene);

	const auto& Vehicles = VehicleSceneQueryData->Vehicles;

	if (std::find(Vehicles.begin(), Vehicles.end(), Vehicle) == Vehicles.end())
	{
		return false;
	}

	PxVehicleDrive4W* DriveVehicle = static_cast<PxVehicleDrive4W*>(Vehicle);

	DriveVehicle->mDriveDynData.setUseAutoGears(bUseAutoGears);

	if (bForce)
	{
		DriveVehicle->mDriveDynData.forceGearChange(static_cast<PxU32>(Gear));
	}
	else
	{
		DriveVehicle->mDriveDynData.startGearChange(static_cast<PxU32>(Gear));
	}

	return true;
}

bool FPhysicsScene::SetVehicleAutomaticGears(PxVehicleWheels* Vehicle, bool bUseAutoGears)
{
	if (!Scene || !Vehicle || !VehicleSceneQueryData)
	{
		return false;
	}

	SCOPED_SCENE_WRITE_LOCK(Scene);

	const auto& Vehicles = VehicleSceneQueryData->Vehicles;

	if (std::find(Vehicles.begin(), Vehicles.end(), Vehicle) == Vehicles.end())
	{
		return false;
	}

	PxVehicleDrive4W* DriveVehicle = static_cast<PxVehicleDrive4W*>(Vehicle);
	DriveVehicle->mDriveDynData.setUseAutoGears(bUseAutoGears);

	return true;
}


void FPhysicsScene::SetDebugVisualization(bool bEnable)
{
	if (!Scene || bDebugVisualization == bEnable)
	{
		return;
	}
	bDebugVisualization = bEnable;

	// eSCALE이 0이면 PhysX가 렌더 버퍼를 아예 채우지 않는다. 꺼 두면 비용이 없다.
	const float On = bEnable ? 1.0f : 0.0f;
	SCOPED_SCENE_WRITE_LOCK(Scene);
	Scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, On);
}

void FPhysicsScene::DrawDebug(FLineBatcher& LineBatcher) const
{
	if (!Scene || !bDebugVisualization)
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
