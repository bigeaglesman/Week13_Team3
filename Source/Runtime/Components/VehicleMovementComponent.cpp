
#include "EnginePCH.h"
#include "Components/VehicleMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsScene.h"
#include "Input/InputSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Math/Quat.h"

#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleWheels.h>

void UVehicleMovementComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!UpdatedComponent || !GetWorld() || !GetWorld()->GetPhysicsScene())
    {
        HTR_LOG(Error, "[Vehicle] MovementComponent has no valid UpdatedComponent or PhysicsScene.");
        return;
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    static const FName WheelComponentNames[4] =
    {
        FName("Wheel_FL"),
        FName("Wheel_FR"),
        FName("Wheel_RL"),
        FName("Wheel_RR")
    };

    for (int32 i = 0; i < 4; ++i)
    {
        WheelVisuals[i] = Cast<UStaticMeshComponent>(Owner->FindComponentByName(WheelComponentNames[i]));

        if (!WheelVisuals[i])
        {
            HTR_LOG(Warning, "[Vehicle] Wheel visual component index {} is missing.", i);
        }
    }

    FPhysicsScene* PhysicsScene = GetWorld()->GetPhysicsScene();
    const FTransform VehicleTransform(UpdatedComponent->GetWorldRotation().Quaternion(), UpdatedComponent->GetWorldLocation(), UpdatedComponent->GetWorldScale3D());

    if (!VehicleInstance.InitVehicle(PhysicsScene, VehicleTransform))
    {
        HTR_LOG(Error, "[Vehicle] Failed to initialize vehicle instance.");
        return;
    }

    if (!PhysicsScene->AddVehicle(VehicleInstance.GetVehicle(), UpdatedComponent))
    {
        HTR_LOG(Error, "[Vehicle] Failed to register vehicle in PhysicsScene.");
        VehicleInstance.TermVehicle();
    }
}

void UVehicleMovementComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!VehicleInstance.IsValid() || !UpdatedComponent || !GetWorld() || DeltaTime <= 0.0f)
    {
        return;
    }

    const FVector InputVector = ConsumeInputVector();
    const FQuat VehicleRotation = UpdatedComponent->GetWorldRotation().Quaternion();

    const float ForwardInput = InputVector.Dot(VehicleRotation.GetForwardVector());
    const float RightInput = InputVector.Dot(VehicleRotation.GetRightVector());

    if (FPhysicsScene* PhysicsScene = GetWorld()->GetPhysicsScene())
    {
        PhysicsScene->SetVehicleInput(VehicleInstance.GetVehicle(), ForwardInput > KINDA_SMALL_NUMBER, ForwardInput < -KINDA_SMALL_NUMBER, FInputSystem::IsKeyDown(EKeyCode::Space), RightInput < -KINDA_SMALL_NUMBER, RightInput > KINDA_SMALL_NUMBER);
    }

    physx::PxVehicleWheels* Vehicle = VehicleInstance.GetVehicle();
    physx::PxVehicleDrive4W* DriveVehicle = static_cast<physx::PxVehicleDrive4W*>(Vehicle);

    const float SteeringInput =
        DriveVehicle->mDriveDynData.getAnalogInput(physx::PxVehicleDrive4WControl::eANALOG_INPUT_STEER_RIGHT) -
        DriveVehicle->mDriveDynData.getAnalogInput(physx::PxVehicleDrive4WControl::eANALOG_INPUT_STEER_LEFT);

    const FQuat CylinderAxisRotation = FQuat::MakeFromAxisAngle(FVector(1.0f, 0.0f, 0.0f), FMath::DegreesToRadians(90.0f));

    for (physx::PxU32 i = 0; i < 4 && i < Vehicle->mWheelsSimData.getNbWheels(); ++i)
    {
        UStaticMeshComponent* WheelComponent = WheelVisuals[i];
        if (!WheelComponent)
        {
            continue;
        }

        const physx::PxVec3& WheelOffset = Vehicle->mWheelsSimData.getWheelCentreOffset(i);
        const physx::PxVehicleWheelData& WheelData = Vehicle->mWheelsSimData.getWheelData(i);

        const float WheelSteerAngle = i < 2 ? SteeringInput * WheelData.mMaxSteer : 0.0f;
        const float WheelRotationAngle = Vehicle->mWheelsDynData.getWheelRotationAngle(i);

        const FQuat SteeringRotation = FQuat::MakeFromAxisAngle(FVector(0.0f, 0.0f, 1.0f), WheelSteerAngle);
        const FQuat SpinRotation = FQuat::MakeFromAxisAngle(FVector(0.0f, 0.0f, 1.0f), WheelRotationAngle);
        const FQuat WheelRotation = SteeringRotation * CylinderAxisRotation * SpinRotation;

        const FVector WheelLocation(WheelOffset.x, WheelOffset.y, WheelOffset.z);
        const FVector WheelScale(WheelData.mRadius, WheelData.mWidth, WheelData.mRadius);

        WheelComponent->SetTransform(FTransform(WheelRotation, WheelLocation, WheelScale));
    }
}

void UVehicleMovementComponent::OnUnregister()
{
    if (VehicleInstance.IsValid() && GetWorld())
    {
        if (FPhysicsScene* PhysicsScene = GetWorld()->GetPhysicsScene())
        {
            PhysicsScene->RemoveVehicle(VehicleInstance.GetVehicle());
        }
    }

    VehicleInstance.TermVehicle();
    Super::OnUnregister();
}
