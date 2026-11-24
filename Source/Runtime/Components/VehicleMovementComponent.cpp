
#include "EnginePCH.h"
#include "Components/VehicleMovementComponent.h"

#include "Engine/World.h"
#include "Physics/PhysicsScene.h"
#include "Input/InputSystem.h"

void UVehicleMovementComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!UpdatedComponent || !GetWorld() || !GetWorld()->GetPhysicsScene())
    {
        HTR_LOG(Error, "[Vehicle] MovementComponent has no valid UpdatedComponent or PhysicsScene.");
        return;
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
