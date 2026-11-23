#pragma once

#include "Containers/Array.h"
#include "Math/Vector.h"

class UWorld;
class FLineBatcher;
struct FBodyInstance;
class FPhysicsSimulationEventCallback;

namespace physx
{
	class PxScene;
}

// simulate 도중 PhysX가 알려 준 충돌 하나. 시뮬레이션이 끝난 뒤 게임 스레드에서 보낸다.
// UE의 FCollisionNotifyInfo 자리다.
struct FCollisionNotifyInfo
{
	FBodyInstance* BodyA = nullptr;   // 바디가 지워지면 nullptr로 바뀐다
	FBodyInstance* BodyB = nullptr;
	bool bNotifyA = false;            // A 쪽 컴포넌트에 OnComponentHit을 보낼지 (A의 bNotifyRigidBodyCollision)
	bool bNotifyB = false;
	FVector ImpactPoint = FVector(0, 0, 0);
	FVector ImpactNormal = FVector(0, 0, 0);   // B에서 A 쪽을 향한다
};

// 월드 하나당 PxScene 하나. 고정 스텝으로 시뮬레이션한다.
// UE 4.27의 FPhysScene_PhysX 자리다.
//
// 멀티스레드: PxScene은 eREQUIRE_RW_LOCK으로 만든다. simulate는 PhysX 작업 스레드(PxDefaultCpuDispatcher)가 나눠 돌리고,
// 씬에 들어간 객체를 엔진 쪽에서 만질 때는 PhysXSceneLock.h의 SCOPED_SCENE_READ/WRITE_LOCK을 건다.
class FPhysicsScene
{
public:
	explicit FPhysicsScene(UWorld* InOwner);
	~FPhysicsScene();

	FPhysicsScene(const FPhysicsScene&) = delete;
	FPhysicsScene& operator=(const FPhysicsScene&) = delete;

	bool IsValid() const { return Scene != nullptr; }

	// GameWorldTick에서 PrePhysics 틱 그룹 다음에 부른다.
	void Tick(float DeltaTime);

	// 디버깅용 Physic Body 렌더링. Collision 쇼 플래그가 켜진 뷰포트가 매 프레임 요청하고 DrawDebug로 그린다.
	// 여러 뷰포트(PIE 게임 뷰포트, 레벨 에디터 뷰포트)가 같은 씬을 보므로, 한 곳이라도 요청하면 다음 스텝에 PhysX가 선을 채운다.
	// 아무도 요청하지 않으면 다음 Tick에 꺼서 비용을 없앤다.
	void RequestDebugVisualization() { bDebugVisualizationRequested = true; }
	void DrawDebug(FLineBatcher& LineBatcher) const;

	physx::PxScene* GetPxScene() const { return Scene; }
	UWorld* GetOwner() const { return Owner; }

	// ---- 충돌 이벤트 ----
	// PhysX 콜백(fetchResults 안)에서 부른다. 바로 보내지 않고 모아 둔다.
	void AddPendingCollisionNotify(const FCollisionNotifyInfo& Info);
	// 바디가 지워질 때 부른다. 모아 둔 이벤트에서 이 바디를 지운다.
	void RemovePendingNotifies(const FBodyInstance* Body);

	static constexpr float FixedStep = 1.0f / 60.0f;
	static constexpr int   MaxSubSteps = 4;   // 이보다 밀리면 남은 시간은 버린다

private:
	void SetDebugVisualization(bool bEnable);
	void StepOnce(float Step);

	// 시뮬레이션 결과(움직인 바디의 위치·회전)를 컴포넌트에 되돌려 쓴다. UE의 SyncComponentsToBodies 자리
	void SyncActiveBodiesToComponents();

	// 모아 둔 충돌 이벤트를 컴포넌트에 보낸다. 씬 락 밖에서 부른다 (받는 쪽이 바디를 만들거나 지울 수 있다).
	// UE의 FPhysScene_PhysX::DispatchPhysNotifications_AssumesLocked 자리
	void DispatchPhysNotifications();

	UWorld*         Owner = nullptr;
	physx::PxScene* Scene = nullptr;
	float           Accumulator = 0.0f;
	bool            bDebugVisualization = false;
	bool            bDebugVisualizationRequested = false;

	FPhysicsSimulationEventCallback* EventCallback = nullptr;
	TArray<FCollisionNotifyInfo>     PendingCollisionNotifies;
};
