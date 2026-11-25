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
    float SuspensionTotalForce = 0.0f;

    float SuspensionJounceVelocityEstimate = 0.0f;
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
    physx::PxVehicleWheels* GetPhysicsVehicle() const { return VehicleInstance.GetVehicle(); }
    void SetDriverInputEnabled(bool bEnabled) { bDriverInputEnabled = bEnabled; }
    float GetEngineRPM() const { return EngineRPM; }
    const FVector& GetLocalLinearVelocity() const { return LocalLinearVelocity; }
    const FVector& GetLocalAngularVelocity() const { return LocalAngularVelocity; }
    const FVehicleWheelTelemetry* GetWheelTelemetry(int32 Index) const { return Index >= 0 && Index < 4 ? &WheelTelemetry[Index] : nullptr; }
    int32 GetCurrentGear() const { return CurrentGear; }
    bool IsAutomaticTransmission() const { return bAutomaticTransmissionRequested; }

    float GetThrottleInput() const { return ThrottleInput; }
    float GetBrakeInput() const { return BrakeInput; }
    float GetSteeringInput() const { return SteeringInput; }
    int32 GetWheelContactCount() const { return WheelContactCount; }
    bool IsWheelInContact(int32 Index) const { return Index >= 0 && Index < 4 && WheelContactStates[Index]; }

    int32 GetDriveModeIndex() const { return VehicleDriveModeIndex; }

protected:
    virtual void OnUnregister() override;

private:
    void UpdateEngineAudio(float DeltaTime);

    UPROPERTY(EditAnywhere)
    bool bMonsterTruckSetup = false;

    UPROPERTY(EditAnywhere)
    bool bShowVehicleHUD = true;

    bool bDriverInputEnabled = true;

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

    // 사용자가 선택한 변속 모드. 중립/후진 중 PhysX가 자동 변속을 잠시 끄더라도 선택값은 유지한다.
    bool bAutomaticTransmissionRequested = true;

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
    bool bPreviousHandbrakeDown = false;
    bool bPreviousDriveModeKeyDown = false;

    float ThrottleInput = 0.0f;
    float BrakeInput = 0.0f;
    float SteeringInput = 0.0f;

    bool WheelContactStates[4] = {};
    int32 WheelContactCount = 0;

    UVehicleHUD* VehicleHUD = nullptr;

    int32 VehicleDriveModeIndex = 0;
};
