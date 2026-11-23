#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/StaticMeshData.h"
#include "Render/MeshRenderData.h"
#include <unordered_map>
#include <vector>
#include "Mesh.generated.h"

class FPrimitiveSceneProxy;
class UBodySetup;

struct FStaticMeshLODResource
{
	FStaticMeshData Data;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
};

UCLASS()
class UStaticMesh : public URenderAsset
{
	GENERATED_BODY()
public:
    UStaticMesh() : SortID(NextSortID++) {}
	virtual ~UStaticMesh() override;

	FStaticMeshData MeshData;

	// Renderer가 실제로 Bind할 런타임 재질 객체
	TArray<UMaterial*> Materials;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;

	const FStaticMeshData& GetMeshData() const { return MeshData; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

public:
    TArray<FStaticMeshLODResource> AdditionalLODs;
    std::array<float, 3> ScreenThresholds{ 0.30f, 0.10f, 0.03f };
    FString SourceObjPath;

    uint32 GetLODCount() const
    {
        return 1u + static_cast<uint32>(AdditionalLODs.Num());
    }

    const FStaticMeshData& GetMeshData(uint32 LODIndex) const
    {
        return LODIndex == 0 ? MeshData : AdditionalLODs[LODIndex - 1].Data;
    }

    FVertexBuffer* GetVertexBuffer(uint32 LODIndex) const
    {
        return LODIndex == 0 ? VertexBuffer.get() : AdditionalLODs[LODIndex - 1].VertexBuffer.get();
    }

    FIndexBuffer* GetIndexBuffer(uint32 LODIndex) const
    {
        return LODIndex == 0 ? IndexBuffer.get() : AdditionalLODs[LODIndex - 1].IndexBuffer.get();
    }

    void RebuildRenderData();
    const FLODSphere& GetLocalLODSphere() const { return LocalLODSphere; }
    const FBox& GetRenderBounds() const { return RenderBounds; }
    TSharedPtr<const FMeshRenderState> GetRenderState(const std::vector<UMaterial*>& Materials);
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

    // 이 메시의 단순 충돌(Simple Collision). 처음 부를 때 정점들의 볼록 껍질을 한 번 쿠킹해 만들고, 같은 메시를 쓰는
    // 컴포넌트가 함께 쓴다. 도형은 메시 원본 단위(스케일 1)다. UE의 UStaticMesh::BodySetup
    // PhysX가 없거나 쿠킹에 실패하면 메시 AABB 박스 하나로 대신한다.
    UBodySetup* GetBodySetup();
    // UBodySetup 소멸자가 부른다 (엔진 종료 때 일괄 삭제에서 BodySetup이 먼저 지워지는 경우).
    void OnBodySetupDestroyed(UBodySetup* Setup) { if (BodySetup == Setup) BodySetup = nullptr; }

    const uint16 SortID;
private:
    UBodySetup* BodySetup = nullptr;
    static inline uint16 NextSortID = 0;
    struct FMaterialSlotsHash
    {
        size_t operator()(const std::vector<UMaterial*>& Slots) const;
    };
    FLODSphere LocalLODSphere;
    FBox RenderBounds;
    std::unordered_map<std::vector<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;
    TArray<FPrimitiveSceneProxy*> RenderProxies;
};
