#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "ClothingSystem/ClothPhysicalMeshData.h"
#include "ClothingAssetCommon.generated.h"

class USkeletalMesh;

// 스켈레탈 메시의 한 섹션을 천으로 만든 에셋. USkeletalMesh가 갖고, 그 메시를 쓰는 모든 컴포넌트가 공유한다.
// UE 4.27의 UClothingAssetCommon 자리다. (UE는 LOD마다 FClothLODDataCommon이 있지만 여기서는 LOD 0 하나뿐)
UCLASS()
class UClothingAssetCommon : public UObject
{
	GENERATED_BODY()
public:
	UClothingAssetCommon() = default;
	virtual ~UClothingAssetCommon();

	// 메시의 섹션에 연결한다. 그 섹션의 렌더 정점마다 어느 시뮬레이션 정점을 따라갈지 대응표를 만든다.
	// UE의 BindToSkeletalMesh (UE는 렌더·시뮬레이션 메시가 달라도 되게 삼각형 보간을 쓰지만, 여기서는 용접 전 같은 정점이라 1:1)
	bool BindToSkeletalMesh(USkeletalMesh* Mesh, int32 SectionIndex);

	// 시뮬레이션용 메시. UE의 LodData[0].PhysicalMeshData
	FClothPhysicalMeshData PhysicalMeshData;

	// 연결된 섹션 번호. 연결 전에는 -1
	int32 BoundSectionIndex = -1;

	// 렌더 정점 번호 → 시뮬레이션 정점 번호. 크기는 메시 전체 정점 수이고, 이 천에 속하지 않는 정점은 -1.
	// 5단계에서 시뮬레이션 결과를 렌더 정점에 덮어쓸 때 쓴다. UE의 섹션 ClothMappingData 자리
	TArray<int32> RenderToSimVertexMap;

	// 이 에셋을 가진 메시. USkeletalMesh::AddClothingAsset이 정한다. 에셋이 먼저 지워지면 메시 목록에서 빠진다.
	USkeletalMesh* OwningMesh = nullptr;
};
