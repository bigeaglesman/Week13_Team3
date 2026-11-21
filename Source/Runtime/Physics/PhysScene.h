#pragma once

#include "Core/Types.h"

class UWorld;

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

	physx::PxScene* GetPxScene() const { return PxSceneInstance; }
	UWorld* GetOwningWorld() const { return OwningWorld; }
	bool IsValid() const { return PxSceneInstance != nullptr; }

private:
	UWorld* OwningWorld = nullptr;
	physx::PxScene* PxSceneInstance = nullptr;

	// 고정 timestep(60Hz) 누적 시간
	float Accumulator = 0.0f;

	static constexpr float FixedDeltaTime = 1.0f / 60.0f;
	static constexpr float MaxFrameDeltaTime = 0.1f;
	static constexpr int32 MaxSubSteps = 4;
};