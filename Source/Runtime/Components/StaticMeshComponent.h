#pragma once

#include "MeshComponent.h"
#include "StaticMeshComponent.generated.h"

UCLASS()
class UStaticMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UStaticMeshComponent();
	virtual ~UStaticMeshComponent();

	virtual void BeginPlay();
	virtual void TickComponent(float DeltaTime);

	virtual const FStaticMeshData* GetMeshData() const override { return StaticMesh ? &StaticMesh->GetMeshData() : nullptr; }

	// 메시 모양 그대로의 물리 도형. 시뮬레이션하면 볼록 껍질(Convex), 아니면 삼각형 메시(바닥·벽용 Complex Collision)
	virtual bool GetBodyGeometry(FKAggregateGeom& OutGeom) const override;

	// 메시 에셋의 단순 충돌(볼록 껍질, UStaticMesh::GetBodySetup)에 컴포넌트 스케일을 곱한 것. Collision 쇼 플래그도 이걸 그린다.
	bool GetSimpleBodyGeometry(FKAggregateGeom& OutGeom) const;

	// 메시가 바뀌면 슬롯 구성이 달라지므로 덮어쓰기를 비운다
	void SetStaticMesh(UStaticMesh* InStaticMesh);
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	virtual UStaticMesh* GetRenderMesh() const override { return StaticMesh; }

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;
	void SubmitToRenderQueue(FRenderQueue& Queue, uint32 LODIndex);

	bool IsEditorIcon() const { return bEditorIcon; }
	void SetEditorIcon(bool IsEditorIcon) { bEditorIcon = IsEditorIcon; }

private:
	UPROPERTY(EditAnywhere)
	UStaticMesh* StaticMesh = nullptr;
	// Mesh의 Section별 Material과 Texture를 보존해 각각의 렌더 패킷으로 제출한다.
	//virtual void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue) override;

	// 조명 Icon Debug arrow용
	bool bEditorIcon = false;
};
