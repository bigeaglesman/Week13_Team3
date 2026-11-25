#pragma once

#include "ClothingSystem/ClothingSimulationContextNv.h"
#include "ClothingSystem/ClothingActorNv.h"
#include "ClothingSystem/ClothConfigNv.h"
#include "ClothingSystem/ClothSimulData.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UClothingAssetCommon;
class UPhysicsAsset;

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
//   TickComponent   → FillContext (컴포넌트 → 컨텍스트) → (처음이면 CreateActor) → Simulate (Solver 한 스텝)
//   OnUnregister    → Shutdown (천 삭제 → Solver 삭제)
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

	// 천 에셋으로 NvCloth 천을 만들어 Solver에 넣는다. 지금 포즈(Context)에서 시작한다. UE의 CreateActor
	// PhysicsAsset이 있으면 그 구·캡슐 중 천 근처의 것을 충돌체로 쓴다.
	bool CreateActor(const UClothingAssetCommon* Asset, const UPhysicsAsset* PhysicsAsset, const FClothingSimulationContextNv& Context);

	// NvCloth가 천 하나에 허용하는 충돌체 최대 개수
	static constexpr int32 MaxCollisionSpheres = 32;
	static constexpr int32 MaxCollisionCapsules = 32;

	// 모든 천을 Solver에서 빼고 지운다. UE의 DestroyActors
	void DestroyActors();

	// 이 에셋으로 만든 천이 이미 있는지
	bool HasActorForAsset(const UClothingAssetCommon* Asset) const;

	// 설정을 바꾼다. 다음 Simulate부터 모든 천에 들어간다 (PIE 중에 디테일 패널에서 바꿔도 바로 반영)
	void SetConfig(const FClothConfigNv& InConfig) { Config = InConfig; }

	// 컨텍스트를 입력으로 Solver를 한 스텝 돌린다. 천이 없으면 아무것도 하지 않는다.
	void Simulate(const FClothingSimulationContextNv& Context);

	const TArray<FClothingActorNv>& GetActors() const { return Actors; }

	// 시뮬레이션 결과를 메시의 천 에셋 순서(GetMeshClothingAssets)대로 담는다. 천이 없는 에셋 칸은 비워 둔다.
	// 스켈레탈 메시 오브젝트가 이걸 받아 렌더 정점에 덮어쓴다. UE의 GetSimulationData
	void GetSimulationData(TArray<FClothSimulData>& OutData, const USkeletalMesh* Mesh) const;

	// 프레임이 크게 튀어도(디버거 정지, 로딩) 천이 폭발하지 않게 한 번에 시뮬레이션하는 최대 시간
	static constexpr float MaxDeltaSeconds = 1.0f / 30.0f;

	// 월드 중력 (FPhysicsScene과 같은 값, m/s²)
	static constexpr float WorldGravityZ = -9.81f;

private:
	// 시뮬레이션 정점을 지금 포즈로 스키닝해 Actor.SkinnedPositions에 넣는다 (시뮬레이션 공간)
	void SkinPhysicsMesh(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const;

	// 설정값을 NvCloth 천에 넣는다 (Phase, 감쇠, 관성, 중력, 바람)
	void ApplyConfig(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const;

	// 고정 입자를 스키닝 위치로 옮기고, 나머지 입자는 Max Distance 구 안에 있게 한다
	void UpdateConstraints(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const;

	// Physics Asset의 구·캡슐을 충돌체로 뽑는다. 천에서 가까운 것부터 NvCloth 한도까지만 넣는다.
	// UE의 ExtractPhysicsAssetCollisions 자리
	void ExtractPhysicsAssetCollisions(FClothingActorNv& Actor, const UPhysicsAsset* PhysicsAsset, const FClothingSimulationContextNv& Context) const;

	// 충돌체를 이번 프레임 뼈 위치로 옮겨 NvCloth에 넣는다
	void UpdateCollisions(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const;

	// 시뮬레이션 결과를 컴포넌트 공간 위치·노멀로 꺼낸다. UE의 GetSimulationData
	void ReadSimulationResults(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const;

	nv::cloth::Solver* Solver = nullptr;
	TArray<FClothingActorNv> Actors;
	FClothConfigNv Config;

	bool bFirstFrame = true;   // 첫 프레임은 항상 텔레포트로 처리한다
};
