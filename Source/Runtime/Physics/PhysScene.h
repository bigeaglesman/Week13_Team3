#pragma once

#include "Core/Types.h"

class UWorld;
class AActor;
class FPhysXSimulationEventCallback;
struct FVector;
struct FHitResult;

namespace physx
{
	class PxScene;
}

// UWorld마다 하나씩 소유하는 물리 씬. PhysX의 PxScene 생명주기와 고정 timestep 시뮬레이션을 맡는다.
class FPhysScene
{
public:
	explicit FPhysScene(UWorld* InOwningWorld);
	~FPhysScene();

	FPhysScene(const FPhysScene&) = delete;
	FPhysScene& operator=(const FPhysScene&) = delete;

	// 고정 timestep 누적기로 PhysX 시뮬레이션을 진행한다 (Game/PIE 월드에서만 호출).
	void Tick(float DeltaTime);

	// PhysX Scene Query (단일 레이캐스트 / 단일 구체 스윕). UnitDir는 정규화된 방향 벡터여야 한다.
	bool RaycastSingle(
		const FVector& Origin,
		const FVector& UnitDir,
		float MaxDistance,
		FHitResult& OutHit,
		AActor* IgnoreActor = nullptr,
		bool bBlockingOnly = false) const;

	bool SweepSphereSingle(
		const FVector& Origin,
		const FVector& UnitDir,
		float MaxDistance,
		float Radius,
		FHitResult& OutHit,
		AActor* IgnoreActor = nullptr,
		bool bBlockingOnly = false) const;

	physx::PxScene* GetPxScene() const { return PxSceneInstance; }
	UWorld* GetOwningWorld() const { return OwningWorld; }
	bool IsValid() const { return PxSceneInstance != nullptr; }

private:
	// fetchResults() 이후 콜백 큐에 쌓인 Overlap/Hit 이벤트를 엔진에 Broadcast한다.
	void FlushSimulationEvents();

	UWorld* OwningWorld = nullptr;
	physx::PxScene* PxSceneInstance = nullptr;
	FPhysXSimulationEventCallback* EventCallback = nullptr;

	// 고정 timestep(60Hz) 누적 시간
	float Accumulator = 0.0f;

	static constexpr float FixedDeltaTime = 1.0f / 60.0f;
	static constexpr float MaxFrameDeltaTime = 0.1f;
	static constexpr int32 MaxSubSteps = 4;
};