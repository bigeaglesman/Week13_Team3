#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Components/ShapeComponent.h"
#include "Physics/PhysicalMaterial.h"

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

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPrimitiveComponent::OnUnregister()
{
	Super::OnUnregister();
	ClearOverlaps();
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

	if (!bUpdatingFromPhysics && IsPhysicsStateCreated())
	{
		const FVector CurrentScale = GetWorldScale3D().GetAbs();
		if ((CurrentScale - CachedBodyScale).SizeSquared() > SMALL_NUMBER)
		{
			RecreatePhysicsState();
		}
		else if (BodyInstance.IsValidBodyInstance())
		{
			BodyInstance.SetBodyTransform(GetWorldTransform());
		}
	}
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
	else if (Property.Name == "bSimulatePhysics" ||
		Property.Name == "bEnableGravity" ||
		Property.Name == "MassInKg" ||
		Property.Name == "LinearDamping" ||
		Property.Name == "AngularDamping")
	{
		RecreatePhysicsState();
	}
	else if (Property.Name == "bGenerateOverlapEvents" ||
		Property.Name == "bBlockComponent")
	{
		UpdateCollisionFilter();
		if (!bGenerateOverlapEvents)
		{
			ClearOverlaps();
		}
	}
	else if (Property.Name == "PhysMaterialPath")
	{
		UPhysicalMaterial* Loaded = PhysMaterialPath.empty()
			? nullptr
			: UPhysicalMaterial::LoadOrGet(PhysMaterialPath);
		SetPhysMaterialOverride(Loaded);
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

void UPrimitiveComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	UWorld* World = GetWorld();
	FPhysScene* PhysScene = World ? World->GetPhysicsScene() : nullptr;
	if (!PhysScene)
	{
		return;
	}

	CachedBodyScale = GetWorldScale3D().GetAbs();

	FBodyShapeDesc ShapeDesc;
	if (!BuildBodyShapeDesc(ShapeDesc))
	{
		return;
	}

	if (!PhysMaterialOverride && !PhysMaterialPath.empty())
	{
		PhysMaterialOverride = UPhysicalMaterial::LoadOrGet(PhysMaterialPath);
	}

	BodyInstance.bSimulatePhysics = bSimulatePhysics;
	BodyInstance.bEnableGravity = bEnableGravity;
	BodyInstance.MassInKg = MassInKg;
	BodyInstance.LinearDamping = LinearDamping;
	BodyInstance.AngularDamping = AngularDamping;
	BodyInstance.PhysMaterialOverride = PhysMaterialOverride;

	// bKinematic, bIsTrigger, 채널/마스크, ActorUniqueID를 InitBody 전에 채운다.
	UpdateCollisionFilter();

	BodyInstance.InitBody(ShapeDesc, GetWorldTransform(), this, PhysScene);
}

void UPrimitiveComponent::OnDestroyPhysicsState()
{
	BodyInstance.TermBody();
	Super::OnDestroyPhysicsState();
}

void UPrimitiveComponent::SetSimulatePhysics(bool bSimulate)
{
	if (bSimulatePhysics == bSimulate)
	{
		return;
	}

	bSimulatePhysics = bSimulate;
	if (IsPhysicsStateCreated())
	{
		RecreatePhysicsState();
	}
}

bool UPrimitiveComponent::IsSimulatingPhysics() const
{
	return BodyInstance.IsInstanceSimulatingPhysics();
}

void UPrimitiveComponent::SetEnableGravity(bool bGravityEnabled)
{
	bEnableGravity = bGravityEnabled;
	BodyInstance.SetEnableGravity(bGravityEnabled);
}

void UPrimitiveComponent::SetPhysMaterialOverride(UPhysicalMaterial* NewPhysMaterial)
{
	PhysMaterialOverride = NewPhysMaterial;
	BodyInstance.SetPhysMaterialOverride(NewPhysMaterial);
}

void UPrimitiveComponent::AddForce(const FVector& Force, bool bAccelChange)
{
	BodyInstance.AddForce(Force, bAccelChange);
}

void UPrimitiveComponent::AddImpulse(const FVector& Impulse, bool bVelChange)
{
	BodyInstance.AddImpulse(Impulse, bVelChange);
}

void UPrimitiveComponent::AddImpulseAtLocation(const FVector& Impulse, const FVector& Location)
{
	BodyInstance.AddImpulseAtPosition(Impulse, Location);
}

void UPrimitiveComponent::SyncComponentToRBPhysics()
{
	if (!BodyInstance.IsInstanceSimulatingPhysics())
	{
		return;
	}

	const FTransform PhysicsTransform = BodyInstance.GetUnrealWorldTransform();
	ComponentVelocity = BodyInstance.GetUnrealWorldVelocity();

	bUpdatingFromPhysics = true;
	SetWorldLocationAndRotation(PhysicsTransform.Location, PhysicsTransform.Rotation, false);
	bUpdatingFromPhysics = false;
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

void UPrimitiveComponent::UpdateCollisionFilter()
{
	const bool bIsShape = (Cast<UShapeComponent>(this) != nullptr);
	AActor* Owner = GetOwner();
	const bool bCollisionEnabled = Owner && Owner->GetActorEnableCollision();

	BodyInstance.bKinematic = bIsShape && !bSimulatePhysics;
	BodyInstance.bIsTrigger = bIsShape && !bSimulatePhysics && !bBlockComponent;
	BodyInstance.ObjectType = bSimulatePhysics ? ECollisionChannel::ECC_PhysicsBody : CollisionObjectType;
	BodyInstance.ActorUniqueID = Owner ? Owner->GetUUID() : 0;

	if (!bCollisionEnabled)
	{
		BodyInstance.BlockMask = 0;
		BodyInstance.OverlapMask = 0;
	}
	else
	{
		// StaticMesh는 기본 Block, Shape는 bBlockComponent 또는 bSimulatePhysics일 때 Block
		const bool bBlocks = !bIsShape || bBlockComponent || bSimulatePhysics;
		BodyInstance.BlockMask = bBlocks ? ECC_AllChannelsMask : 0u;
		BodyInstance.OverlapMask = bGenerateOverlapEvents ? ECC_AllChannelsMask : 0u;
	}

	// 이미 PxRigidActor가 생성되어 있다면 Shape의 FilterData와 Trigger 플래그를 즉시 갱신한다.
	BodyInstance.UpdatePhysicsFilterData();
}

void UPrimitiveComponent::ClearOverlaps()
{
	// 삭제/등록 해제/충돌 비활성화된 도형을 상대의 OverlapInfos에 남기지 않는다.
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other)
		{
			Other->EndComponentOverlap(this);
		}
	}
}