#include "EnginePCH.h"
#include "StaticMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Physics/AggregateGeom.h"
#include "Physics/BodySetup.h"


// StaticMesh 컴포넌트를 초기화한다.
UStaticMeshComponent::UStaticMeshComponent()
{
	StaticMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
}

// StaticMesh 컴포넌트의 소멸을 처리한다.
UStaticMeshComponent::~UStaticMeshComponent()
{
}

// 부모 컴포넌트의 시작 처리를 호출한다.
void UStaticMeshComponent::BeginPlay()
{
	Super::BeginPlay();
}

bool UStaticMeshComponent::GetSimpleBodyGeometry(FKAggregateGeom& OutGeom) const
{
	// 에디터 아이콘(조명 화살표 등)은 바디가 없다.
	UBodySetup* Setup = (StaticMesh && !bEditorIcon) ? StaticMesh->GetBodySetup() : nullptr;
	if (!Setup || Setup->AggGeom.IsEmpty())
	{
		return false;
	}

	// 메시 에셋의 도형은 스케일 1이다. 바디 포즈에는 스케일이 없으므로 도형에 곱한다.
	OutGeom = Setup->AggGeom.GetScaled(GetWorldScale3D());
	return true;
}

bool UStaticMeshComponent::GetBodyGeometry(FKAggregateGeom& OutGeom) const
{
	// 움직이는 바디: PhysX가 삼각형 메시 시뮬레이션을 지원하지 않아 메시 에셋의 볼록 껍질을 쓴다. (오목한 부분은 메워진다)
	if (BodyInstance.bSimulatePhysics)
	{
		return GetSimpleBodyGeometry(OutGeom);
	}

	const FStaticMeshData* MeshData = (StaticMesh && !bEditorIcon) ? GetMeshData() : nullptr;
	if (!MeshData || MeshData->Vertices.Num() < 3)
	{
		return false;
	}

	// 움직이지 않는 바디: 메시 모양 그대로 (Complex Collision).
	// 바디 포즈에는 스케일이 없으므로 정점에 곱한다. 음수 스케일이면 GetScaled가 감기 방향도 뒤집는다.
	FKAggregateGeom Unscaled;
	FKTriMeshElem& Elem = Unscaled.TriMeshElems[Unscaled.TriMeshElems.Add(FKTriMeshElem())];
	Elem.Vertices.Reserve(MeshData->Vertices.Num());
	for (const FVertex& V : MeshData->Vertices)
	{
		Elem.Vertices.Add(V.Position);
	}
	Elem.Indices = MeshData->Indices;

	OutGeom = Unscaled.GetScaled(GetWorldScale3D());
	return true;
}

// 부모 컴포넌트의 프레임 갱신을 호출한다.
void UStaticMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
	if (StaticMesh == InStaticMesh)
		return;

	StaticMesh = InStaticMesh;
	ClearOverrideMaterials();
	MarkRenderStateDirty();
	OnTransformDirty();
}

int32 UStaticMeshComponent::GetNumMaterials() const
{
	return StaticMesh ? static_cast<int32>(StaticMesh->GetMeshData().MaterialSlots.Num()) : 0;
}

FString UStaticMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!StaticMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
		return FString();
	return StaticMesh->GetMeshData().MaterialSlots[SlotIndex].Name;
}

UMaterial* UStaticMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return StaticMesh ? StaticMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

void UStaticMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	if (!StaticMesh) return;
	uint32 SelectedLOD = 0;
	if (const FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		SelectedLOD = SelectLOD(*Proxy, ViewContext);
	}
	SubmitToRenderQueue(RenderQueue, SelectedLOD);
}

// 지정한 LOD의 Section으로 패킷을 만든다.
void UStaticMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, uint32 LODIndex)
{
    const FPrimitiveSceneProxy* Proxy = GetSceneProxy();
    if (!StaticMesh || !Proxy) return;
    const FMeshRenderState* State = Proxy->GetRenderState();
    if (!State) return;
    if (LODIndex >= State->LODCount) LODIndex = 0;
    const FCachedMeshLOD& LOD = State->LODs[LODIndex];
    for (uint32 I = 0; I < LOD.NumSections; ++I)
    {
        const FCachedMeshSection& Section = State->Sections[LOD.FirstSection + I];
        FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
        Packet.Proxy = Proxy;
        Packet.Mesh = StaticMesh;
        Packet.Material = Section.Material;
        Packet.StartIndex = Section.StartIndex;
        Packet.IndexCount = Section.IndexCount;
        Packet.LODIndex = static_cast<uint8>(LODIndex);
    }
}
/*
// Section별 Material·Texture와 인덱스 범위를 보존해 패킷을 제출한다.
void UStaticMeshComponent::SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue)
{
	if (!StaticMesh)
		return;

	const FStaticMeshData& MeshData = StaticMesh->GetMeshData();

	for (const FStaticMeshSection& Section : MeshData.Sections)
	{
		// 슬롯마다 덮어쓰기가 있으면 그것, 없으면 메시(OBJ/MTL)의 기본 머티리얼
		UMaterial* SectionMaterial = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!SectionMaterial)
			continue;

		FRenderPacket rp;
		rp.mesh = StaticMesh;
		rp.model = GetWorldMatrix();
		rp.StartIndex = Section.StartIndex;
		rp.IndexCount = Section.IndexCount;
		rp.material = SectionMaterial;

		RenderQueue.Enqueue(rp);
	}
}
*/
