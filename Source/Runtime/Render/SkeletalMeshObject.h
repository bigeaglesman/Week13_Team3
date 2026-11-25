#pragma once

#include "Render/SkeletalMesh.h"
#include "ClothingSystem/ClothSimulData.h"

class FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObject(USkeletalMesh* InMesh);
	virtual ~FSkeletalMeshObject() = default;

	virtual void Update(const TArray<FMatrix>& ComponentSpaceTransforms) = 0;
	virtual FVertexBuffer* GetVertexBuffer(uint32 LOD) const = 0;

	FIndexBuffer* GetIndexBuffer(uint32 LOD) const
	{
		return SkeletalMesh->GetRenderData().LODRenderData[LOD].IndexBuffer.get();
	}

	const FBox& GetBounds() const { return Bounds; }
	virtual const TArray<FVertex>* GetCPUSkinnedVertices() const { return nullptr; }
	// GPU 스킨 경로만 돌려준다. CPU 경로는 정점이 이미 스키닝되어 있어 nullptr
	virtual const FStructuredBuffer* GetBoneMatrixBuffer() const { return nullptr; }
	// 이번 포즈가 Bounds 밖으로 나가면 여유를 두고 넓히고 true를 돌려준다
	bool UpdateBounds();
	void SetDebugWeightBone(int32 BoneIndex) { DebugWeightBone = BoneIndex; }

	// 천 시뮬레이션 결과. 인덱스는 USkeletalMesh::GetMeshClothingAssets()의 순서이고, 비어 있으면 그 천은 스키닝 결과를 그대로 쓴다.
	// 다음 Update에서 렌더 정점에 덮어쓴다. CPU 스킨 경로에서만 반영된다. (UE의 SkeletalMeshObject 천 데이터 갱신 자리)
	void SetClothSimulData(TArray<FClothSimulData>&& InData) { ClothSimulData = std::move(InData); }

protected:
	void UpdateRefToLocalMatrices(const TArray<FMatrix>& ComponentSpaceTransforms);
	void SkinVerticesCPU(TArray<FVertex>& OutVertices, FBox& OutBounds) const;

	// 스키닝한 정점 중 천 섹션 정점의 위치·노멀을 시뮬레이션 결과로 바꾸고, 바운드를 넓힌다.
	void ApplyClothSimulData(TArray<FVertex>& InOutVertices, FBox& InOutBounds) const;

	TArray<FClothSimulData> ClothSimulData;

	int32 DebugWeightBone = -1;

	USkeletalMesh* SkeletalMesh;
	TArray<FMatrix> RefToLocal;
	FBox Bounds;
	FBox SkinnedBounds;
};

class FSkeletalMeshObjectCPUSkin : public FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObjectCPUSkin(USkeletalMesh* InMesh);

	void Update(const TArray<FMatrix>& ComponentSpaceTransforms) override;
	FVertexBuffer* GetVertexBuffer(uint32 LOD) const override { return FinalVertexBuffer.get(); }
	const TArray<FVertex>* GetCPUSkinnedVertices() const override { return &SkinnedVertices; }

private:
	void UploadVertices();

	TUniquePtr<FVertexBuffer> FinalVertexBuffer;
	TArray<FVertex> SkinnedVertices;
};

// 언리얼 기본 GPU 스킨 경로: 정점은 움직이지 않고, 패스의 버텍스 셰이더가 본 행렬을 섞어 스키닝한다.
class FSkeletalMeshObjectGPUSkin : public FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObjectGPUSkin(USkeletalMesh* InMesh);

	bool IsValid() const;
	void Update(const TArray<FMatrix>& ComponentSpaceTransforms) override;
	FVertexBuffer* GetVertexBuffer(uint32 LOD) const override;
	const FStructuredBuffer* GetBoneMatrixBuffer() const override { return BoneMatrixBuffer.get(); }
	// 피킹처럼 CPU에서 정점이 필요할 때만 현재 포즈로 계산한다
	const TArray<FVertex>* GetCPUSkinnedVertices() const override;

private:
	void UpdateSkinnedBoundsFromBones();
	void UploadBoneMatrices();

	TUniquePtr<FStructuredBuffer> BoneMatrixBuffer;
	mutable TArray<FVertex> CPUVertices;
	mutable bool bCPUVerticesDirty = false;
};
