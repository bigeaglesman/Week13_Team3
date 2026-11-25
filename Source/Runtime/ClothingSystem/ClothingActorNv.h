#pragma once

#include "Math/Vector.h"
#include "ClothingSystem/ClothCollisionData.h"

class UClothingAssetCommon;

namespace nv
{
	namespace cloth
	{
		class Cloth;
		class Fabric;
	}
}

// 천 에셋 하나로 만든 실제 NvCloth 천 하나. FClothingSimulationNv가 만들고, 갖고, 지운다.
// UE 4.27의 FClothingActorNv 자리다.
struct FClothingActorNv
{
	const UClothingAssetCommon* AssetCreatedFrom = nullptr;   // 이 천을 만든 에셋 (메시가 소유)

	nv::cloth::Fabric* Fabric = nullptr;   // 쿠킹 결과 (제약 구조: Phase, 휴지 길이, tether)
	nv::cloth::Cloth*  Cloth = nullptr;    // 입자 상태. Solver에 들어가 있다

	// Phase마다 종류 (ClothFabricPhaseType: 세로·가로·굽힘·전단). 쿠커가 알려 주고, 설정을 고를 때 쓴다
	TArray<int32> PhaseTypes;

	// 삼각형 감김 순서로 구한 면 노멀이 메시 노멀과 반대면 true. 결과 노멀을 뒤집는다
	bool bFlipNormals = false;

	// Physics Asset에서 뽑은 충돌체 (천 근처의 구·캡슐만, NvCloth 한도 32개 안에서). 매 프레임 뼈를 따라 옮긴다
	FClothCollisionData Collisions;

	// 이번 프레임 스키닝한 시뮬레이션 정점 위치 (시뮬레이션 공간 = 컴포넌트 회전·위치 기준, 스케일 적용)
	// 고정 입자와 Max Distance 구의 중심으로 쓴다
	TArray<FVector> SkinnedPositions;

	// 시뮬레이션 결과 (컴포넌트 공간). 5단계에서 렌더 정점에 덮어쓴다. UE의 GetSimulationData 결과 자리
	TArray<FVector> SimPositions;
	TArray<FVector> SimNormals;
};
