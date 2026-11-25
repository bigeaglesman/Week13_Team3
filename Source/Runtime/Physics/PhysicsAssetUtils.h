#pragma once

#include "Core/Types.h"

class USkeletalMesh;
class UPhysicsAsset;

// 스켈레탈 메시에서 Physics Asset을 자동으로 만들 때의 설정
struct FPhysAssetCreateParams
{
	// 메시 전체 크기 대비 이 비율보다 짧은 뼈(손가락 마디, 끝 뼈 등)는 바디를 만들지 않는다.
	// 메시 단위(cm/m)와 상관없이 같은 결과가 나오게 비율로 둔다.
	float MinBoneSizeRatio = 0.04f;

	// 바디 질량의 총합. 캡슐 부피 비율로 나눠 준다.
	float TotalMassKg = 70.0f;

	// 관절 기본 한계 (±도)
	float TwistLimitDeg = 30.0f;
	float Swing1LimitDeg = 45.0f;
	float Swing2LimitDeg = 45.0f;
};

// UE 4.27의 FPhysicsAssetUtils::CreateFromSkeletalMesh 자리 (UE는 에디터 모듈에 있다).
// Physics Asset Editor가 없을 때도 래그돌을 돌릴 수 있도록 런타임에 둔다.
namespace FPhysicsAssetUtils
{
	// 1. 정점을 가중치가 가장 큰 뼈에 배정하고, 뼈 로컬 공간에서 그 정점들의 AABB를 잰다.
	// 2. 충분히 큰 뼈마다 AABB를 감싸는 캡슐 바디를 만든다. 캡슐 축 = AABB의 가장 긴 축
	// 3. 바디가 있는 가장 가까운 조상 뼈와 관절로 잇는다. 관절 위치 = 자식 뼈 원점, Twist 축 = 캡슐 축
	// 반환된 에셋은 호출한 쪽이 소유한다. 실패하면 nullptr
	UPhysicsAsset* CreateFromSkeletalMesh(const USkeletalMesh* Mesh, const FPhysAssetCreateParams& Params = FPhysAssetCreateParams());
	bool AddCapsuleBodyForBone(USkeletalMesh& Mesh, int32 BoneIndex);
}
