
#pragma once

#include "PawnMovementComponent.h"
#include "Physics/VehicleInstance.h"
#include "VehicleMovementComponent.generated.h"

UCLASS()
class UVehicleMovementComponent : public UPawnMovementComponent
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime) override;

protected:
    virtual void OnUnregister() override;

private:
    FVehicleInstance VehicleInstance;
};
