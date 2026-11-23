#include "EnginePCH.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXSDK.h"
#include "Physics/PhysicsFilter.h"

using namespace physx;

namespace
{
	constexpr float MaxPhysicsDeltaTime = 1.0f / 30.0f;   // UE MaxPhysicsDeltaTime. 프레임이 튀어도 물체가 뚫고 나가지 않게
}

FPhysScene::FPhysScene(UWorld* InOwner) 
	: Owner(InOwner)
{
	PxPhysics* Physics = PhysXSDK::Get();
	if (!Physics) return;

	PxSceneDesc Desc(Physics->getTolerancesScale());
	Desc.gravity = PxVec3(0.0f, 0.0f, -9.81f);
	Desc.cpuDispatcher = PhysXSDK::Dispatcher();
	Desc.filterShader = HitoriFilterShader;
	Desc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;   // getActiveActors로 움직인 바디만 받기 위해

	Scene = Physics->createScene(Desc);
	if (!Scene)
		HTR_LOG(Error, "[Physics] createScene failed");
}

FPhysScene::~FPhysScene()
{
	if (bSimulating) Scene->fetchResults(true);   // 계산 중인 씬은 release할 수 없다
	if (Scene) { Scene->release(); Scene = nullptr; }
}

void FPhysScene::AddActor(PxRigidActor* Actor)
{
	if (Scene && Actor) Scene->addActor(*Actor);
}

void FPhysScene::RemoveActor(PxRigidActor* Actor)
{
	if (Scene && Actor) Scene->removeActor(*Actor);
}

void FPhysScene::StartFrame(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f) return;   // simulate는 0 이하 시간을 받지 않는다

	const float StepTime = DeltaTime < MaxPhysicsDeltaTime ? DeltaTime : MaxPhysicsDeltaTime;
	for (std::function<void(float)>& Callback : OnPhysSceneStep)
		Callback(StepTime);

	Scene->simulate(StepTime);
	bSimulating = true;
}

void FPhysScene::EndFrame()
{
	if (!bSimulating) return;

	Scene->fetchResults(true);
	bSimulating = false;

	SyncComponentsToBodies();
	DispatchPhysNotifications();
}

void FPhysScene::AddPendingCollision(const FCollisionNotifyInfo& Info)
{
	PendingCollisions.Add(Info);
}

void FPhysScene::SyncComponentsToBodies()
{
	// 8단계(FBodyInstance)에서 채운다
}

void FPhysScene::DispatchPhysNotifications()
{
	PendingCollisions.Empty();   // 9단계(Hit 이벤트)에서 채운다
}