#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Physics/AggregateGeom.h"

namespace
{
	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

FTransform UPrimitiveComponent::GetBodyWorldTransform() const
{
	const FQuat Rotation = GetAttachParent()
		? GetWorldRotation().Quaternion()
		: GetRelativeRotationQuat();   // 부모가 없으면 쿼터니언을 그대로 쓴다
	return FTransform(Rotation, GetWorldLocation());
}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
	CreatePhysicsState();
}

bool UPrimitiveComponent::ShouldCreatePhysicsState() const
{
	return BodyInstance.bSimulatePhysics || BodyInstance.bEnablePhysicsCollision;
}

void UPrimitiveComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	FKAggregateGeom Geom;
	if (GetBodyGeometry(Geom))
	{
		BodyInstance.InitBody(this, Geom, GetBodyWorldTransform(), GetWorld()->GetPhysicsScene());
	}
}

void UPrimitiveComponent::OnDestroyPhysicsState()
{
	BodyInstance.TermBody();
	Super::OnDestroyPhysicsState();
}

void UPrimitiveComponent::SyncComponentToRBPhysics()
{
	if (!BodyInstance.IsSimulating())
	{
		return;
	}

	const FTransform Pose = BodyInstance.GetUnrealWorldTransform();

	// 부모가 없으면 쿼터니언 그대로 넣는다. FRotator를 거치면 Pitch ±90도 근처에서 값이 튄다.
	if (!GetAttachParent())
	{
		FTransform Transform = GetTransform();
		Transform.Location = Pose.Location;
		Transform.Rotation = Pose.Rotation;
		SetTransform(Transform);
	}
	else
	{
		SetWorldLocationAndRotation(Pose.Location, Pose.Rotation.ToFRotator(), false);
	}
}

void UPrimitiveComponent::OnUnregister()
{
	Super::OnUnregister();
	// 삭제/등록 해제된 도형을 상대의 OverlapInfos에 남기지 않는다.
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other) Other->EndComponentOverlap(this);
	}
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
	return new FPrimitiveSceneProxy(this);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);

	// 키네마틱 바디는 컴포넌트를 따라간다. 시뮬레이션 중인 바디는 SetBodyTransform이 무시한다.
	if (BodyInstance.IsValidBodyInstance())
		BodyInstance.SetBodyTransform(GetBodyWorldTransform());
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	return Super::MoveComponent(Delta, NewRotation, bSweep, Hit);
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	if (Property.Name == "bCastShadow" ||
		Property.Name == "bReceiveShadow" ||
		Property.Name == "bReceivesDecals" ||
		Property.Name == "TranslucencySortPriority")
	{
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
	}
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	if (!Other) return false;

	for (const FOverlapInfo& Info : OverlapInfos)
	{
		if (Info.OtherComp == Other)
			return true;
	}

	return false;
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp || OtherComp == this || IsOverlappingComponent(OtherComp))
	{
		return;
	}

	OverlapInfos.Add({ OtherComp });

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentBeginOverlap.Broadcast(this, OtherActor, OtherComp);
	if (Owner && OtherActor)
	{
		Owner->NotifyActorBeginOverlap(OtherActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) return;

	bool bRemoved = false;

	for (int32 Index = 0;Index < OverlapInfos.Num(); ++Index)
	{
		if (OverlapInfos[Index].OtherComp == OtherComp)
		{
			OverlapInfos.RemoveAt(Index, 1);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
		return;


	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyActorEndOverlap(OtherActor);
	}
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) 
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentHit.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyHit(OtherActor);
	}
}
