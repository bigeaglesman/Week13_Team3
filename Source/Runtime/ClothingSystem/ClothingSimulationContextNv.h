#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"

// 매 프레임 컴포넌트가 천 시뮬레이션에 넘기는 입력. 시뮬레이션은 컴포넌트를 직접 보지 않고 이것만 읽는다.
// UE 4.27의 FClothingSimulationContextNv 자리다. (FillContext가 채우고 Simulate가 읽는다)
struct FClothingSimulationContextNv
{
	float DeltaSeconds = 0.0f;              // 이번 프레임 시뮬레이션 시간 (최대 MaxDeltaSeconds로 자른다)

	FMatrix ComponentToWorld;               // 컴포넌트 월드 행렬. 위치·회전은 NvCloth 로컬 프레임이 되고, 스케일은 따로 쓴다
	FVector ComponentScale = FVector(1.0f, 1.0f, 1.0f);   // 컴포넌트 월드 스케일. 천 입자 위치에 미리 곱한다

	TArray<FMatrix> BoneTransforms;         // 뼈의 컴포넌트 공간 행렬 (애니메이션 결과). 6단계 충돌체가 따라간다
	TArray<FMatrix> RefToLocals;            // 바인드 포즈 → 지금 포즈 스키닝 행렬 (InverseBindPose × BoneTransform). 고정 입자가 따라간다

	FVector WindVelocity = FVector::ZeroVector;   // 월드 공간 바람 속도 (m/s)

	bool bTeleport = false;                 // true면 이번 프레임은 관성 없이 천을 새 위치로 옮긴다 (순간이동, 첫 프레임)
};
