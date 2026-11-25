#include "EnginePCH.h"
#include "Components/EncoreCargoComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/VehicleMovementComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleWheels.h>
#include "Physics/PhysXSceneLock.h"

using namespace physx;

UEncoreCargoComponent::UEncoreCargoComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// The wig needs the final skinned pose, including this frame's PostPhysics ragdoll blend.
	PrimaryComponentTick.TickGroup = ETickingGroup::PostUpdateWork;
}

void UEncoreCargoComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Truck = GetOwner();
	Movement = Cast<UVehicleMovementComponent>(Truck->FindComponentByName(FName("VehicleMovementComponent")));
	Lid = Cast<UBoxComponent>(Truck->FindComponentByName(FName("MangoLidBody")));
	Hitori = Cast<USceneComponent>(Truck->FindComponentByName(FName("HitoriInCarton")));
	Mannequin = Cast<USkeletalMeshComponent>(Truck->FindComponentByName(FName("DivingMannequin")));
	Wig = Cast<USceneComponent>(Truck->FindComponentByName(FName("MannequinWig")));
	HeadBone = Mannequin ? Mannequin->FindBoneIndex("Head") : -1;
	if (!Movement || !Lid || !Hitori || !Mannequin || !Wig || HeadBone < 0)
	{
		HTR_LOG(Warning, "[Encore] Incomplete cargo: movement={}, lid={}, Hitori={}, mannequin={}, wig={}, head={}.",
			Movement != nullptr, Lid != nullptr, Hitori != nullptr, Mannequin != nullptr, Wig != nullptr, HeadBone);
	}
	if (Hitori) HitoriStartZ = Hitori->GetRelativeLocation().Z;
	if (Wig && HeadBone >= 0) Wig->SetWorldTransform(Mannequin->GetBoneWorldMatrix(HeadBone), false);
	// An already open editor world may still contain the old free-falling body settings.
	if (Lid && (Lid->BodyInstance.bSimulatePhysics || !Lid->BodyInstance.bEnablePhysicsCollision))
	{
		Lid->DestroyPhysicsState();
		Lid->BodyInstance.bSimulatePhysics = false;
		Lid->BodyInstance.bEnablePhysicsCollision = true;
		Lid->CreatePhysicsState();
	}
}

void UEncoreCargoComponent::AttachLid()
{
	PxVehicleWheels* Vehicle = Movement->GetPhysicsVehicle();
	PxRigidDynamic* Chassis = Vehicle ? Vehicle->getRigidDynamicActor() : nullptr;
	PxRigidActor* LidBody = Lid->BodyInstance.GetPxRigidActor();
	PxRigidDynamic* LidActor = LidBody ? LidBody->is<PxRigidDynamic>() : nullptr;
	if (!Chassis || !LidActor || !Chassis->getScene() || Chassis->getScene() != LidActor->getScene()) return;

	SCOPED_SCENE_WRITE_LOCK(Chassis->getScene());
	const PxQuat HingeAxis(PxHalfPi, PxVec3(0, 0, 1)); // revolute X maps to carton Y
	const PxTransform LidFrame(PxVec3(Lid->GetScaledBoxExtent().X, 0, 0), HingeAxis);
	// SyncVehiclesToComponents may only have queued a kinematic target this tick.
	// Compute the hinge directly from the authored truck-local frame, even after checkpoint travel.
	const PxTransform LidRelative = ToPx(Lid->GetTransform());
	const PxTransform TruckFrame = LidRelative * LidFrame;
	const PxTransform LidPose = Chassis->getGlobalPose() * LidRelative;
	LidActor->setGlobalPose(LidPose);
	if (!PxRigidBodyExt::setMassAndUpdateInertia(*LidActor, Lid->BodyInstance.MassInKg)) return;
	Hinge = PxRevoluteJointCreate(Chassis->getScene()->getPhysics(), Chassis, TruckFrame, LidActor, LidFrame);
	if (Hinge)
	{
		Hinge->setLimit(PxJointAngularLimitPair(-0.03f, 1.53f));
		// Under hard braking, correct the lightweight flap toward the chassis, preserving free hinge rotation.
		Hinge->setProjectionLinearTolerance(0.012f);
		Hinge->setConstraintFlag(PxConstraintFlag::ePROJECT_TO_ACTOR0, true);
		Hinge->setConstraintFlag(PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, true);
		Hinge->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);
		Hinge->setDriveForceLimit(12.0f);
		Hinge->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, true);
		Hinge->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, true);
		LidActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);
		LidActor->setLinearVelocity(PxRigidBodyExt::getVelocityAtPos(*Chassis, LidPose.p));
		LidActor->setAngularVelocity(Chassis->getAngularVelocity());
		Lid->BodyInstance.bSimulatePhysics = true;
		Lid->DetachFromParent(EAttachmentRule::KeepWorld);
		HTR_LOG(Info, "[Encore] Carton lid attached to chassis.");
	}
}

void UEncoreCargoComponent::LaunchMannequin()
{
	// Keep the driving pose and its scale when promoting the passenger to independent physics bodies.
	Mannequin->Stop();
	Mannequin->DetachFromParent(EAttachmentRule::KeepWorld);
	Mannequin->DestroyPhysicsState();
	Mannequin->BodyInstance.bSimulatePhysics = true;
	Mannequin->CreatePhysicsState();
	const auto& Bodies = Mannequin->GetBodies();
	if (Bodies.IsEmpty())
	{
		HTR_LOG(Error, "[Encore] The mannequin has no simulated bodies.");
		return;
	}

	const FMatrix TruckToWorld = GetOwner()->GetRootComponent()->GetWorldMatrix();
	const float ForwardSpeed = FMath::Clamp(Movement->GetLocalLinearVelocity().X, 7.0f, 11.0f);
	const PxVec3 Launch = ToPx(TruckToWorld.TransformVectorNoScale(FVector(ForwardSpeed, 0, 0))) + PxVec3(0, 0, 3.8f);
	const PxVec3 Spin = ToPx(TruckToWorld.TransformVectorNoScale(FVector(0, 3.5f, 0)));
	SCOPED_SCENE_WRITE_LOCK(Bodies[0]->GetPxRigidActor()->getScene());
	const PxVec3 Center = Bodies[0]->GetPxRigidActor()->getGlobalPose().p;
	for (FBodyInstance* Body : Bodies)
	{
		if (!Body->IsSimulating()) continue;
		PxRigidDynamic* Actor = Body->GetPxRigidActor()->is<PxRigidDynamic>();
		Actor->setLinearVelocity(Launch + Spin.cross(Actor->getGlobalPose().p - Center));
		Actor->setAngularVelocity(Spin);
	}
	HTR_LOG(Info, "[Encore] Mannequin launched with {} bodies and {} joints.", Bodies.Num(), Mannequin->GetNumConstraints());
}

void UEncoreCargoComponent::TickComponent(float DeltaTime)
{
	if (!Movement) return;
	if (Lid && !Hinge)
	{
		LidRetryDelay -= DeltaTime;
		if (LidRetryDelay <= 0.0f)
		{
			AttachLid();
			LidRetryDelay = 0.2f;
		}
	}
	UGameInstance* Game = UGameplayStatics::GetGameInstance(GetWorld());
	if (!Game) return;

	if (!bDived && Game->GetValue("EncoreDive") != 0)
	{
		bDived = true;
		if (Mannequin) LaunchMannequin();
	}

	const bool bRevealed = Game->GetValue("EncoreReveal") != 0;
	const float Bump = FMath::Clamp(std::abs(Movement->GetLocalLinearVelocity().Z) * 0.14f, 0.0f, 0.45f);
	const float LidAngle = bRevealed ? 1.48f : (bDived ? 0.0f : 0.30f + Bump);
	float ActualAngle = 0.0f;
	if (Hinge)
	{
		SCOPED_SCENE_WRITE_LOCK(Hinge->getScene());
		ActualAngle = Hinge->getAngle();
		Hinge->setDriveVelocity(FMath::Clamp((LidAngle - ActualAngle) * 8.0f, -4.5f, 4.5f));
	}

	// The reveal follows the physical opening so the head clears the flap.
	float HeightOffset = bDived ? -0.34f : 0.0f;
	if (bRevealed)
		HeightOffset = -0.34f + 0.97f * FMath::Clamp(ActualAngle / 1.2f, 0.0f, 1.0f);
	if (Hitori)
	{
		FVector Position = Hitori->GetRelativeLocation();
		const float Step = 2.8f * DeltaTime;
		Position.Z += FMath::Clamp(HitoriStartZ + HeightOffset - Position.Z, -Step, Step);
		Hitori->SetRelativeLocation(Position);
	}
	if (Wig && HeadBone >= 0) Wig->SetWorldTransform(Mannequin->GetBoneWorldMatrix(HeadBone), false);
}

void UEncoreCargoComponent::OnUnregister()
{
	if (Hinge)
	{
		SCOPED_SCENE_WRITE_LOCK(Hinge->getScene());
		Hinge->release();
		Hinge = nullptr;
	}
	Super::OnUnregister();
}
