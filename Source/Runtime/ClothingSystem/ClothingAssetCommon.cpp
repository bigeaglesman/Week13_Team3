#include "EnginePCH.h"
#include "ClothingSystem/ClothingAssetCommon.h"
#include "ClothingSystem/ClothingMeshUtils.h"
#include "Render/SkeletalMesh.h"

#include <map>

UClothingAssetCommon::~UClothingAssetCommon()
{
	if (OwningMesh)
	{
		OwningMesh->OnClothingAssetDestroyed(this);
		OwningMesh = nullptr;
	}
}

bool UClothingAssetCommon::BindToSkeletalMesh(USkeletalMesh* Mesh, int32 SectionIndex)
{
	if (!Mesh || Mesh->GetRenderData().LODRenderData.Num() == 0)
	{
		return false;
	}

	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	if (SectionIndex < 0 || SectionIndex >= LOD.Sections.Num())
	{
		return false;
	}

	// 시뮬레이션 정점을 위치 키로 찾을 수 있게 표를 만든다.
	std::map<ClothingMeshUtils::FWeldKey, int32> SimIndexByKey;
	for (int32 SimIndex = 0; SimIndex < PhysicalMeshData.GetNumVerts(); ++SimIndex)
	{
		SimIndexByKey.emplace(ClothingMeshUtils::MakeWeldKey(PhysicalMeshData.Vertices[SimIndex]), SimIndex);
	}

	// 섹션이 쓰는 렌더 정점마다 같은 자리의 시뮬레이션 정점을 붙인다.
	RenderToSimVertexMap.Init(-1, LOD.BindVertices.Num());
	const FSkeletalMeshSection& Section = LOD.Sections[SectionIndex];
	for (uint32 I = Section.StartIndex; I < Section.StartIndex + Section.IndexCount; ++I)
	{
		const uint32 RenderIndex = LOD.Indices[I];
		auto Found = SimIndexByKey.find(ClothingMeshUtils::MakeWeldKey(LOD.BindVertices[RenderIndex].Position));
		if (Found != SimIndexByKey.end())
		{
			RenderToSimVertexMap[RenderIndex] = Found->second;
		}
	}

	BoundSectionIndex = SectionIndex;
	return true;
}
