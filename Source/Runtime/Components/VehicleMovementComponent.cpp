
#include "EnginePCH.h"
#include "Components/VehicleMovementComponent.h"

#include "Engine/World.h"
#include "Physics/PhysicsScene.h"

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

    if (!PhysicsScene->AddVehicle(VehicleInstance.GetVehicle()))
    {
        HTR_LOG(Error, "[Vehicle] Failed to register vehicle in PhysicsScene.");
        VehicleInstance.TermVehicle();
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
