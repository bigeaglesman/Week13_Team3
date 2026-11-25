#include "EnginePCH.h"
#include "ClothingSystem/ClothingAssetFactory.h"
#include "ClothingSystem/ClothingAssetCommon.h"
#include "ClothingSystem/ClothingMeshUtils.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"

#include <map>

namespace
{
	// 렌더 정점의 스킨 가중치를 시뮬레이션 정점 형식으로 옮긴다. 가중치가 0인 칸은 뺀다.
	FClothVertBoneData ToClothBoneData(const FSkinWeight& Weight)
	{
		FClothVertBoneData Out;
		for (int32 I = 0; I < MaxBoneInfluences; ++I)
		{
			if (Weight.Weight[I] > 0.0f)
			{
				Out.BoneIndices[Out.NumInfluences] = Weight.BoneIndex[I];
				Out.BoneWeights[Out.NumInfluences] = Weight.Weight[I];
				++Out.NumInfluences;
			}
		}
		return Out;
	}

	// 섹션 높이로 Max Distance를 정한다. 위쪽은 고정(0), 아래로 갈수록 멀리 움직일 수 있다.
	void ComputeMaxDistancesByHeight(FClothPhysicalMeshData& Data, float PinHeightRatio, float MaxDistanceScale)
	{
		float MinZ = FLT_MAX, MaxZ = -FLT_MAX;
		for (const FVector& V : Data.Vertices)
		{
			MinZ = std::min(MinZ, V.Z);
			MaxZ = std::max(MaxZ, V.Z);
		}

		const float Height = MaxZ - MinZ;
		if (Height <= 0.0f)
		{
			return;
		}

		const float Pin = std::clamp(PinHeightRatio, 0.0f, 0.99f);
		for (int32 V = 0; V < Data.GetNumVerts(); ++V)
		{
			const float FromTop = (MaxZ - Data.Vertices[V].Z) / Height;   // 맨 위 0, 맨 아래 1
			Data.MaxDistances[V] = FromTop <= Pin
				? 0.0f
				: (FromTop - Pin) / (1.0f - Pin) * Height * MaxDistanceScale;
		}
	}
}

UClothingAssetCommon* FClothingAssetFactory::CreateFromSkeletalMesh(USkeletalMesh* TargetMesh, const FSkeletalMeshClothBuildParams& Params)
{
	if (!TargetMesh || TargetMesh->GetRenderData().LODRenderData.Num() == 0)
	{
		return nullptr;
	}

	const FSkeletalMeshLODRenderData& LOD = TargetMesh->GetRenderData().LODRenderData[0];
	if (Params.SourceSection < 0 || Params.SourceSection >= LOD.Sections.Num())
	{
		// 어느 번호가 어떤 재질인지 알 수 있게 섹션 목록을 같이 찍는다.
		HTR_LOG(Warning, "[Cloth] Section {} does not exist (mesh has {} sections).", Params.SourceSection, LOD.Sections.Num());
		const TArray<FSkeletalMaterialSlot>& Slots = TargetMesh->GetMaterialSlots();
		for (int32 I = 0; I < LOD.Sections.Num(); ++I)
		{
			const uint32 Slot = LOD.Sections[I].MaterialSlotIndex;
			HTR_LOG(Warning, "[Cloth]   Section {}: {}", I, Slot < static_cast<uint32>(Slots.Num()) ? Slots[Slot].Name : FString("?"));
		}
		return nullptr;
	}
	const FSkeletalMeshSection& Section = LOD.Sections[Params.SourceSection];

	FClothPhysicalMeshData Data;
	Data.Reset(0);

	// 1) 용접: 섹션이 쓰는 렌더 정점을 위치로 묶어 시뮬레이션 정점 하나로 만든다.
	std::map<ClothingMeshUtils::FWeldKey, int32> SimIndexByKey;
	std::map<uint32, int32> SimIndexByRender;
	for (uint32 I = Section.StartIndex; I < Section.StartIndex + Section.IndexCount; ++I)
	{
		const uint32 RenderIndex = LOD.Indices[I];
		if (SimIndexByRender.count(RenderIndex))
		{
			continue;
		}

		const FVertex& RenderVertex = LOD.BindVertices[RenderIndex];
		auto [It, bNew] = SimIndexByKey.emplace(ClothingMeshUtils::MakeWeldKey(RenderVertex.Position), Data.GetNumVerts());
		if (bNew)
		{
			Data.Vertices.Add(RenderVertex.Position);
			Data.Normals.Add(FVector::ZeroVector);
			Data.BoneData.Add(ToClothBoneData(LOD.SkinWeights[RenderIndex]));
		}
		Data.Normals[It->second] += RenderVertex.Normal;   // 합친 정점들의 노멀 평균
		SimIndexByRender.emplace(RenderIndex, It->second);
	}

	// 2) 삼각형을 시뮬레이션 정점 번호로 바꾼다. 용접으로 두 꼭짓점이 겹친 삼각형은 버린다.
	for (uint32 I = Section.StartIndex; I + 2 < Section.StartIndex + Section.IndexCount; I += 3)
	{
		const int32 A = SimIndexByRender[LOD.Indices[I]];
		const int32 B = SimIndexByRender[LOD.Indices[I + 1]];
		const int32 C = SimIndexByRender[LOD.Indices[I + 2]];
		if (A == B || B == C || C == A)
		{
			continue;
		}
		Data.Indices.Add(static_cast<uint32>(A));
		Data.Indices.Add(static_cast<uint32>(B));
		Data.Indices.Add(static_cast<uint32>(C));
	}

	const int32 NumVerts = Data.GetNumVerts();
	if (NumVerts < 3 || Data.Indices.Num() < 3)
	{
		HTR_LOG(Warning, "[Cloth] Section {} has no usable triangles.", Params.SourceSection);
		return nullptr;
	}

	// 3) 정점마다 나머지 값 채우기: 노멀 정규화, 최대 뼈 영향 수
	for (int32 V = 0; V < NumVerts; ++V)
	{
		Data.Normals[V] = Data.Normals[V].GetSafeNormal(SMALL_NUMBER, UpVector);
		Data.MaxBoneWeights = std::max(Data.MaxBoneWeights, Data.BoneData[V].NumInfluences);
	}

	// 4) 고정할 곳(Max Distance)과 질량
	Data.MaxDistances.Init(0.0f, NumVerts);
	ComputeMaxDistancesByHeight(Data, Params.PinHeightRatio, Params.MaxDistanceScale);
	Data.CalculateInverseMasses();

	if (Data.NumFixedVerts == 0 || Data.NumFixedVerts == NumVerts)
	{
		HTR_LOG(Warning, "[Cloth] Section {}: {} of {} verts fixed. Check PinHeightRatio.", Params.SourceSection, Data.NumFixedVerts, NumVerts);
	}

	// 5) 에셋을 만들어 메시에 붙이고 섹션에 연결한다.
	UClothingAssetCommon* Asset = NewObject<UClothingAssetCommon>();
	Asset->PhysicalMeshData = std::move(Data);
	TargetMesh->AddClothingAsset(Asset);
	Asset->BindToSkeletalMesh(TargetMesh, Params.SourceSection);

	HTR_LOG(Info, "[Cloth] Section {} -> {} sim verts ({} fixed), {} tris (from {} render indices)",
		Params.SourceSection, Asset->PhysicalMeshData.GetNumVerts(), Asset->PhysicalMeshData.NumFixedVerts,
		Asset->PhysicalMeshData.Indices.Num() / 3, Section.IndexCount);
	return Asset;
}
