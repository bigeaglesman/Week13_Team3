
#pragma once

#include "PawnMovementComponent.h"
#include "Physics/VehicleInstance.h"
#include "Math/Vector.h"

#include "VehicleMovementComponent.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class USpringArmComponent;
class UVehicleHUD;

struct FVehicleWheelTelemetry
{
    bool bIsInAir = true;
    bool bHasContact = false;

    float LongitudinalSlip = 0.0f;
    float LateralSlip = 0.0f;
    float TireFriction = 0.0f;

    float SuspensionJounce = 0.0f;
    float SuspensionSpringForce = 0.0f;

    float SuspensionJounceVelocity = 0.0f;
    float SuspensionSpringComponent = 0.0f;
    float SuspensionDamperComponent = 0.0f;

    int32 TireSurfaceType = -1;
};

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
    const FVector& GetLocalLinearVelocity() const { return LocalLinearVelocity; }
    const FVector& GetLocalAngularVelocity() const { return LocalAngularVelocity; }
    const FVehicleWheelTelemetry* GetWheelTelemetry(int32 Index) const { return Index >= 0 && Index < 4 ? &WheelTelemetry[Index] : nullptr; }
    int32 GetCurrentGear() const { return CurrentGear; }
    bool IsAutomaticTransmission() const { return bAutomaticTransmission; }

    float GetThrottleInput() const { return ThrottleInput; }
    float GetBrakeInput() const { return BrakeInput; }
    float GetSteeringInput() const { return SteeringInput; }
    int32 GetWheelContactCount() const { return WheelContactCount; }
    bool IsWheelInContact(int32 Index) const { return Index >= 0 && Index < 4 && WheelContactStates[Index]; }

    int32 GetDriveModeIndex() const { return VehicleDriveModeIndex; }

protected:
    virtual void OnUnregister() override;

private:
    UStaticMeshComponent* WheelVisuals[4] = {};
    UCameraComponent* VehicleCamera = nullptr;
    USpringArmComponent* VehicleSpringArm = nullptr;
    float BaseCameraFOV = 60.0f;
    float CurrentCameraFOV = 60.0f;
    float BaseSpringArmLength = 13.5f;
    float CurrentSpringArmLength = 13.5f;
    float BaseCameraLagSpeed = 14.0f;
    float CurrentCameraLagSpeed = 14.0f;
    FVehicleInstance VehicleInstance;

    bool bIsVehicleInAir = true;
    bool bAutomaticTransmission = true;

    float VehicleSpeed = 0.0f;
    float EngineRPM = 0.0f;
    int32 CurrentGear = 2;

    FVector LocalLinearVelocity = FVector(0.0f, 0.0f, 0.0f);
    FVector LocalAngularVelocity = FVector(0.0f, 0.0f, 0.0f);
    FVehicleWheelTelemetry WheelTelemetry[4] = {};

    bool bPreviousNeutralKeyDown = false;
    bool bPreviousAutomaticToggleKeyDown = false;
    bool bPreviousGearUpKeyDown = false;
    bool bPreviousGearDownKeyDown = false;

    float ThrottleInput = 0.0f;
    float BrakeInput = 0.0f;
    float SteeringInput = 0.0f;

    bool WheelContactStates[4] = {};
    int32 WheelContactCount = 0;

    UVehicleHUD* VehicleHUD = nullptr;

    int32 VehicleDriveModeIndex = 0;
    bool bPreviousDriveModeKeyDown = false;
};
