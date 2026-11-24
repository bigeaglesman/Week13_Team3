
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

    const FTransform VehicleTransform(UpdatedComponent->GetWorldRotation().Quaternion(), UpdatedComponent->GetWorldLocation(), UpdatedComponent->GetWorldScale3D());

    if (!VehicleInstance.InitVehicle(GetWorld()->GetPhysicsScene(), VehicleTransform))
    {
        HTR_LOG(Error, "[Vehicle] Failed to initialize vehicle instance.");
    }
}

void UVehicleMovementComponent::OnUnregister()
{
    VehicleInstance.TermVehicle();
    Super::OnUnregister();
}
