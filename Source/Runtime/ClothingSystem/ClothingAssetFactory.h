#pragma once

class USkeletalMesh;
class UClothingAssetCommon;

// 섹션 하나로 천 에셋을 만들 때의 옵션. UE 4.27의 FSkeletalMeshClothBuildParams 자리다.
struct FSkeletalMeshClothBuildParams
{
	int32 SourceSection = -1;      // 천으로 만들 섹션 번호 (LOD 0)

	// UE는 Max Distance를 붓으로 칠하지만, 여기서는 섹션 높이로 자동으로 정한다.
	//   위에서부터 PinHeightRatio 비율까지 → 0 (고정, 어깨에 붙는 부분)
	//   그 아래 → 0에서 시작해 맨 아래가 (섹션 높이 × MaxDistanceScale)이 되도록 커진다
	float PinHeightRatio = 0.1f;
	float MaxDistanceScale = 1.0f;
};

// 스켈레탈 메시의 섹션으로 천 에셋을 만든다. UE 4.27의 UClothingAssetFactory 자리다.
// (UE는 에디터의 "Create Clothing Data from Section" 메뉴가 부르고, 여기서는 컴포넌트가 처음 천을 쓸 때 부른다)
class FClothingAssetFactory
{
public:
	// 섹션 정점을 용접해 시뮬레이션 메시를 만들고, 메시에 붙이고(AddClothingAsset), 섹션에 연결(BindToSkeletalMesh)한다.
	// 실패하면 nullptr. 성공하면 메시가 에셋을 소유한다.
	static UClothingAssetCommon* CreateFromSkeletalMesh(USkeletalMesh* TargetMesh, const FSkeletalMeshClothBuildParams& Params);
};
