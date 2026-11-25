#pragma once

#include "ClothingSystem/ClothingSimulationContextNv.h"

class USkeletalMeshComponent;

namespace nv
{
	namespace cloth
	{
		class Solver;
	}
}

// 스켈레탈 메시 컴포넌트 하나의 천 시뮬레이션. NvCloth Solver를 하나 갖고, 그 컴포넌트의 천들을 한 번에 돌린다.
// UE 4.27의 FClothingSimulationNv 자리다.
//   OnRegister      → Initialize (Solver 생성)
//   TickComponent   → FillContext (컴포넌트 → 컨텍스트) → Simulate (Solver 한 스텝)
//   OnUnregister    → Shutdown (Solver 삭제)
class FClothingSimulationNv
{
public:
	FClothingSimulationNv() = default;
	~FClothingSimulationNv();

	FClothingSimulationNv(const FClothingSimulationNv&) = delete;
	FClothingSimulationNv& operator=(const FClothingSimulationNv&) = delete;

	// Factory를 받아와서 Solver를 만든다.
	bool Initialize();
	void Shutdown();

	bool IsInitialized() const { return Solver != nullptr; }

	// 컴포넌트의 이번 프레임 상태(뼈, 트랜스폼, 시간)를 컨텍스트에 복사한다.
	void FillContext(const USkeletalMeshComponent* Component, float DeltaTime, FClothingSimulationContextNv& OutContext) const;

	// 컨텍스트를 입력으로 Solver를 한 스텝 돌린다. 천이 없으면 아무것도 하지 않는다.
	void Simulate(const FClothingSimulationContextNv& Context);

	// 프레임이 크게 튀어도(디버거 정지, 로딩) 천이 폭발하지 않게 한 번에 시뮬레이션하는 최대 시간
	static constexpr float MaxDeltaSeconds = 1.0f / 30.0f;

private:
	nv::cloth::Solver* Solver = nullptr;

	bool bFirstFrame = true;   // 첫 프레임은 항상 텔레포트로 처리한다
};
