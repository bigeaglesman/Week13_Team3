#pragma once

class UWorld;
class FLineBatcher;

namespace physx
{
	class PxScene;
}

// 월드 하나당 PxScene 하나. 고정 스텝으로 시뮬레이션한다.
// UE 4.27의 FPhysScene_PhysX 자리다.
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

	// 디버깅용 Physic Body 렌더링. Show Flag Collision이 켜졌을 때 PhysX 시각화 선을 LineBatcher로 넘긴다.
	void SetDebugVisualization(bool bEnable);
	void DrawDebug(FLineBatcher& LineBatcher) const;

	physx::PxScene* GetPxScene() const { return Scene; }
	UWorld* GetOwner() const { return Owner; }

	static constexpr float FixedStep = 1.0f / 60.0f;
	static constexpr int   MaxSubSteps = 4;   // 이보다 밀리면 남은 시간은 버린다

private:
	void StepOnce(float Step);

	UWorld*         Owner = nullptr;
	physx::PxScene* Scene = nullptr;
	float           Accumulator = 0.0f;
	bool            bDebugVisualization = false;
};
