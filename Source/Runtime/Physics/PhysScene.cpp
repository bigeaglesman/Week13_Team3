#include "EnginePCH.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXPublic.h"

#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

FPhysScene::FPhysScene(UWorld* InOwningWorld)
	: OwningWorld(InOwningWorld)
{
	if (!GPhysXSDK || !GPhysXCpuDispatcher)
	{
		HTR_LOG(Error, "[PhysX] Cannot create FPhysScene: PhysX is not initialized");
		return;
	}

	PxSceneDesc SceneDesc(GPhysXSDK->getTolerancesScale());
	SceneDesc.gravity = U2PVector(FVector(0.0f, 0.0f, -9.81f));
	SceneDesc.cpuDispatcher = GPhysXCpuDispatcher;
	SceneDesc.filterShader = PxDefaultSimulationFilterShader;

	// 5단계에서 움직인 Dynamic 액터만 골라 동기화할 때 필요하다.
	SceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;
	// 안정적인 접촉 생성을 위해 PCM(Persistent Contact Manifold)을 켠다.
	SceneDesc.flags |= PxSceneFlag::eENABLE_PCM;

	PxSceneInstance = GPhysXSDK->createScene(SceneDesc);
	if (!PxSceneInstance)
	{
		HTR_LOG(Error, "[PhysX] GPhysXSDK->createScene failed");
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
		PxSceneInstance->release();
		PxSceneInstance = nullptr;
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

	int32 Steps = 0;
	while (Accumulator >= FixedDeltaTime && Steps < MaxSubSteps)
	{
		PxSceneInstance->simulate(FixedDeltaTime);
		PxSceneInstance->fetchResults(true);

		Accumulator -= FixedDeltaTime;
		++Steps;
	}

	// 최대 서브스텝을 다 돌고도 남은 시간은 버려 시뮬레이션이 영구히 뒤처지는 현상을 막는다.
	if (Steps == MaxSubSteps && Accumulator >= FixedDeltaTime)
	{
		Accumulator = 0.0f;
	}
}