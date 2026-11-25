#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"

// 매 프레임 컴포넌트가 천 시뮬레이션에 넘기는 입력. 시뮬레이션은 컴포넌트를 직접 보지 않고 이것만 읽는다.
// UE 4.27의 FClothingSimulationContextNv 자리다. (FillContext가 채우고 Simulate가 읽는다)
struct FClothingSimulationContextNv
{
	float DeltaSeconds = 0.0f;              // 이번 프레임 시뮬레이션 시간 (최대 MaxDeltaSeconds로 자른다)

	FMatrix ComponentToWorld;               // 컴포넌트 월드 행렬. 천 입자는 컴포넌트 공간에 있으므로 중력·바람을 이걸로 돌린다

	TArray<FMatrix> BoneTransforms;         // 뼈의 컴포넌트 공간 행렬 (애니메이션 결과). 고정 입자가 이걸 따라간다

	FVector WindVelocity = FVector::ZeroVector;   // 월드 공간 바람 속도 (m/s)

	bool bTeleport = false;                 // true면 이번 프레임은 관성 없이 천을 새 위치로 옮긴다 (순간이동, 첫 프레임)
};
