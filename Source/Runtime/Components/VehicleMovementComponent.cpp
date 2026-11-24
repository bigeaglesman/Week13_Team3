
#include "EnginePCH.h"
#include "Components/VehicleMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsScene.h"
#include "Input/InputSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Math/Quat.h"

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


    FPhysicsScene* PhysicsScene = GetWorld()->GetPhysicsScene();

    if (!PhysicsScene)
    {
        return;
    }

    physx::PxVehicleWheels* Vehicle = VehicleInstance.GetVehicle();

    PhysicsScene->SetVehicleInput(Vehicle, ForwardInput > KINDA_SMALL_NUMBER, ForwardInput < -KINDA_SMALL_NUMBER, FInputSystem::IsKeyDown(EKeyCode::Space), RightInput < -KINDA_SMALL_NUMBER, RightInput > KINDA_SMALL_NUMBER);

    FVehicleWheelState WheelStates[4];

    if (!PhysicsScene->GetVehicleWheelStates(Vehicle, WheelStates, 4, bIsVehicleInAir))
    {
        return;
    }

    const FQuat CylinderAxisRotation = FQuat::MakeFromAxisAngle(FVector(1.0f, 0.0f, 0.0f), FMath::DegreesToRadians(90.0f));

    for (int32 i = 0; i < 4; ++i)
    {
        UStaticMeshComponent* WheelComponent = WheelVisuals[i];

        if (!WheelComponent)
        {
            continue;
        }

        const FVehicleWheelState& WheelState = WheelStates[i];

        const float WheelSteerAngle = WheelState.SteerAngle;
        const float WheelRotationAngle = WheelState.WheelRotationAngle;

        const FVector WheelLocation = WheelState.WheelCenterOffset - WheelState.SuspensionTravelDirection * WheelState.SuspensionJounce;

        const FQuat SteeringRotation = FQuat::MakeFromAxisAngle(FVector(0.0f, 0.0f, 1.0f), WheelSteerAngle);
        const FQuat SpinRotation = FQuat::MakeFromAxisAngle(FVector(0.0f, 0.0f, 1.0f), WheelRotationAngle);
        const FQuat WheelRotation = SteeringRotation * CylinderAxisRotation * SpinRotation;

        const FVector WheelScale(WheelState.WheelRadius, WheelState.WheelWidth, WheelState.WheelRadius);

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
