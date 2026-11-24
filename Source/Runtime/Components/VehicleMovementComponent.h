
#pragma once

#include "PawnMovementComponent.h"
#include "Physics/VehicleInstance.h"
#include "VehicleMovementComponent.generated.h"

class UStaticMeshComponent;

UCLASS()
class UVehicleMovementComponent : public UPawnMovementComponent
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime) override;

    bool IsVehicleInAir() const { return bIsVehicleInAir; }
    float GetVehicleSpeed() const { return VehicleSpeed; }
    float GetEngineRPM() const { return EngineRPM; }
    int32 GetCurrentGear() const { return CurrentGear; }
    bool IsAutomaticTransmission() const { return bAutomaticTransmission; }

protected:
    virtual void OnUnregister() override;

private:
    UStaticMeshComponent* WheelVisuals[4] = {};
    FVehicleInstance VehicleInstance;

    bool bIsVehicleInAir = true;
    bool bAutomaticTransmission = true;

    float VehicleSpeed = 0.0f;
    float EngineRPM = 0.0f;
    int32 CurrentGear = 2;

    bool bPreviousNeutralKeyDown = false;
    bool bPreviousAutomaticToggleKeyDown = false;
    bool bPreviousGearUpKeyDown = false;
    bool bPreviousGearDownKeyDown = false;
};
