#pragma once
#include <PxPhysicsAPI.h>
#include <functional>
#include "Containers/Containers.h"

class UWorld;
class FPhysXSimEventCallback;
struct FCollisionNotifyInfo {};

// UE FPhysScene_PhysX. UWorld 하나당 PxScene 하나를 소유한다.
class FPhysScene
{
public:
	explicit FPhysScene(UWorld* InOwner);
	~FPhysScene();

	void StartFrame(float DeltaTime);   // OnPhysSceneStep 호출 후 simulate
	void EndFrame();                    // fetchResults → 컴포넌트 동기화 → 충돌 이벤트 전달

	void AddActor(physx::PxRigidActor* Actor);
	void RemoveActor(physx::PxRigidActor* Actor);

	// onContact는 fetchResults 도중에 불리므로 여기 쌓아 두고 EndFrame에서 처리한다.
	void AddPendingCollision(const FCollisionNotifyInfo& Info);

	TArray<std::function<void(float)>> OnPhysSceneStep;   // simulate 직전 호출 (차량 업데이트용)

	physx::PxScene* GetPxScene() const { return Scene; }

private:
	void SyncComponentsToBodies();
	void DispatchPhysNotifications();

	UWorld* Owner = nullptr;
	physx::PxScene* Scene = nullptr;
	FPhysXSimEventCallback* EventCallback = nullptr;
	TArray<FCollisionNotifyInfo> PendingCollisions;

	bool bSimulating = false;
};
