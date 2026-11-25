
#include "EnginePCH.h"
#include "Components/VehicleMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Physics/PhysicsScene.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Math/Quat.h"
#include "UI/VehicleHUD.h"
#include "UI/UserWidget.h"

#include <cmath>
#include <vehicle/PxVehicleComponents.h>

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

    VehicleCamera = Cast<UCameraComponent>(Owner->FindComponentByName(FName("VehicleTestCamera")));

    if (VehicleCamera)
    {
        BaseCameraFOV = VehicleCamera->GetFieldOfView();
        CurrentCameraFOV = BaseCameraFOV;
    }
    else
    {
        HTR_LOG(Warning, "[Vehicle] VehicleTestCamera component is missing.");
    }

    VehicleSpringArm = Cast<USpringArmComponent>(Owner->FindComponentByName(FName("VehicleSpringArm")));

    if (VehicleSpringArm)
    {
        BaseSpringArmLength = VehicleSpringArm->TargetArmLength;
        CurrentSpringArmLength = BaseSpringArmLength;
        BaseCameraLagSpeed = VehicleSpringArm->CameraLagSpeed;
        CurrentCameraLagSpeed = BaseCameraLagSpeed;
    }
    else
    {
        HTR_LOG(Warning, "[Vehicle] VehicleSpringArm component is missing.");
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

    UPrimitiveComponent* VehiclePrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);
    const bool bNotifyRigidBodyCollision = VehiclePrimitive && VehiclePrimitive->BodyInstance.bNotifyRigidBodyCollision;

    if (!VehicleInstance.InitVehicle(PhysicsScene, VehicleTransform, bNotifyRigidBodyCollision))
    {
        HTR_LOG(Error, "[Vehicle] Failed to initialize vehicle instance.");
        return;
    }

    if (!PhysicsScene->AddVehicle(VehicleInstance.GetVehicle(), UpdatedComponent))
    {
        HTR_LOG(Error, "[Vehicle] Failed to register vehicle in PhysicsScene.");
        VehicleInstance.TermVehicle();
        return;
    }

    if (GetWorld()->GetPlayerPawn() == GetPawnOwner())
    {
        VehicleHUD = Cast<UVehicleHUD>(CreateWidget(GetWorld(), "Assets/UI/VehicleHUD.widget"));

        if (VehicleHUD)
        {
            VehicleHUD->SetVehicleMovementComponent(this);
            VehicleHUD->OnDestroyed = [this]() { VehicleHUD = nullptr; };
            VehicleHUD->AddToViewport(10);
        }
        else
        {
            HTR_LOG(Warning, "[Vehicle] Failed to create VehicleHUD.");
        }
    }
}

void UVehicleMovementComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!VehicleInstance.IsValid() || !UpdatedComponent || !GetWorld() || DeltaTime <= 0.0f)
    {
        return;
    }

    APlayerController* Controller = GetPawnOwner() ? GetPawnOwner()->GetController() : nullptr;
    const auto IsKeyDown = [Controller](EKeyCode Key) { return Controller && Controller->IsKeyDown(static_cast<int32>(Key)); };

    const float ForwardInput = static_cast<float>(IsKeyDown(EKeyCode::W)) - static_cast<float>(IsKeyDown(EKeyCode::S));
    const float RightInput = static_cast<float>(IsKeyDown(EKeyCode::D)) - static_cast<float>(IsKeyDown(EKeyCode::A));

    FPhysicsScene* PhysicsScene = GetWorld()->GetPhysicsScene();

    if (!PhysicsScene)
    {
        return;
    }

    physx::PxVehicleWheels* Vehicle = VehicleInstance.GetVehicle();

    const bool bNeutralKeyDown = IsKeyDown(EKeyCode::N);
    const bool bAutomaticToggleKeyDown = IsKeyDown(EKeyCode::M);
    const bool bGearUpKeyDown = IsKeyDown(EKeyCode::E);
    const bool bGearDownKeyDown = IsKeyDown(EKeyCode::Q);
    const bool bDriveModeKeyDown = IsKeyDown(EKeyCode::V);

    const bool bNeutralPressed = bNeutralKeyDown && !bPreviousNeutralKeyDown;
    const bool bAutomaticTogglePressed = bAutomaticToggleKeyDown && !bPreviousAutomaticToggleKeyDown;
    const bool bGearUpPressed = bGearUpKeyDown && !bPreviousGearUpKeyDown;
    const bool bGearDownPressed = bGearDownKeyDown && !bPreviousGearDownKeyDown;
    const bool bDriveModePressed = bDriveModeKeyDown && !bPreviousDriveModeKeyDown;

    bPreviousNeutralKeyDown = bNeutralKeyDown;
    bPreviousAutomaticToggleKeyDown = bAutomaticToggleKeyDown;
    bPreviousGearUpKeyDown = bGearUpKeyDown;
    bPreviousGearDownKeyDown = bGearDownKeyDown;
    bPreviousDriveModeKeyDown = bDriveModeKeyDown;

    FVehicleDriveState DriveState;

    if (!PhysicsScene->GetVehicleDriveState(Vehicle, DriveState))
    {
        return;
    }

    VehicleSpeed = DriveState.LinearSpeed;
    EngineRPM = DriveState.EngineRPM;
    CurrentGear = DriveState.CurrentGear;

    const bool bVehicleInReverse =
        DriveState.CurrentGear == physx::PxVehicleGearsData::eREVERSE ||
        DriveState.TargetGear == physx::PxVehicleGearsData::eREVERSE;

    const bool bVehicleInNeutral =
        DriveState.CurrentGear == physx::PxVehicleGearsData::eNEUTRAL &&
        DriveState.TargetGear == physx::PxVehicleGearsData::eNEUTRAL;

    const bool bTransmissionTemporarilyManual = bVehicleInReverse || bVehicleInNeutral;

    // 전진 기어 상태에서는 PhysX의 실제 자동 변속 상태와 사용자의 선택을 동기화한다.
    // 중립과 후진은 PhysX에서 자동 변속이 일시적으로 꺼지므로 사용자의 선택값을 유지한다.
    if (!bTransmissionTemporarilyManual)
    {
        bAutomaticTransmissionRequested = DriveState.bUseAutoGears;
    }

    if (bDriveModePressed)
    {
        if (std::abs(DriveState.LinearSpeed) > 1.0f || std::abs(ForwardInput) > KINDA_SMALL_NUMBER)
        {
            HTR_LOG(Warning, "[Vehicle] Drive mode can only be changed while stopped.");
        }
        else
        {
            const int32 NextModeIndex = (VehicleDriveModeIndex + 1) % 3;
            const EVehicleDriveMode NextMode = static_cast<EVehicleDriveMode>(NextModeIndex);

            if (PhysicsScene->SetVehicleDriveMode(Vehicle, NextMode))
            {
                VehicleDriveModeIndex = NextModeIndex;
                HTR_LOG(Info, "[Vehicle] Drive Mode Changed | Mode={}", VehicleDriveModeIndex);
            }
        }
    }

    LocalLinearVelocity = DriveState.LocalLinearVelocity;
    LocalAngularVelocity = DriveState.LocalAngularVelocity;

    if (VehicleCamera)
    {
        const float SpeedAlpha = FMath::Clamp(std::abs(VehicleSpeed) / 65.0f, 0.0f, 1.0f);
        const float TargetFOV = BaseCameraFOV + 12.0f * SpeedAlpha;
        const float FOVAlpha = FMath::Clamp(DeltaTime * 4.0f, 0.0f, 1.0f);

        CurrentCameraFOV += (TargetFOV - CurrentCameraFOV) * FOVAlpha;
        VehicleCamera->SetFieldOfView(CurrentCameraFOV);
    }

    if (VehicleSpringArm)
    {
        const float SpeedAlpha = FMath::Clamp(std::abs(VehicleSpeed) / 65.0f, 0.0f, 1.0f);
        const float TargetArmLength = BaseSpringArmLength + 2.5f * SpeedAlpha;
        const float ArmAlpha = FMath::Clamp(DeltaTime * 2.0f, 0.0f, 1.0f);

        CurrentSpringArmLength += (TargetArmLength - CurrentSpringArmLength) * ArmAlpha;
        VehicleSpringArm->TargetArmLength = CurrentSpringArmLength;

        const float TargetCameraLagSpeed = BaseCameraLagSpeed * (1.0f - 0.2f * SpeedAlpha);
        const float LagAlpha = FMath::Clamp(DeltaTime * 3.0f, 0.0f, 1.0f);

        CurrentCameraLagSpeed += (TargetCameraLagSpeed - CurrentCameraLagSpeed) * LagAlpha;
        VehicleSpringArm->CameraLagSpeed = CurrentCameraLagSpeed;
    }

    if (bAutomaticTogglePressed)
    {
        bAutomaticTransmissionRequested = !bAutomaticTransmissionRequested;

        // 중립/후진에서는 선택값만 바꾸고, 전진 기어로 복귀할 때 실제 PhysX 설정을 적용한다.
        if (!bTransmissionTemporarilyManual)
        {
            PhysicsScene->SetVehicleAutomaticGears(Vehicle, bAutomaticTransmissionRequested);
        }

        HTR_LOG(Info, "[Vehicle] Transmission Mode | Automatic={}", bAutomaticTransmissionRequested);
    }

    bool bAccelerate = false;
    bool bBrake = false;

    const bool bForwardCommand = ForwardInput > KINDA_SMALL_NUMBER;
    const bool bReverseCommand = ForwardInput < -KINDA_SMALL_NUMBER;

    if (bNeutralPressed)
    {
        PhysicsScene->SetVehicleGear(Vehicle, physx::PxVehicleGearsData::eNEUTRAL, false, true);
        bAccelerate = false;
        bBrake = false;
    }
    else if (bForwardCommand)
    {
        const bool bMustStop = bVehicleInReverse || bVehicleInNeutral;

        if (bMustStop && DriveState.LinearSpeed > 0.5f)
        {
            bBrake = true;
        }
        else
        {
            if (bVehicleInReverse || bVehicleInNeutral)
            {
                PhysicsScene->SetVehicleGear(Vehicle, physx::PxVehicleGearsData::eFIRST, bAutomaticTransmissionRequested, true);
            }

            bAccelerate = true;
        }
    }
    else if (bReverseCommand)
    {
        const bool bMustStop = !bVehicleInReverse;

        if (bMustStop && DriveState.LinearSpeed > 0.5f)
        {
            bBrake = true;
        }
        else
        {
            if (!bVehicleInReverse)
            {
                PhysicsScene->SetVehicleGear(Vehicle, physx::PxVehicleGearsData::eREVERSE, false, true);
            }

            bAccelerate = true;
        }
    }

    if (!bAutomaticTransmissionRequested && !bVehicleInReverse && !bVehicleInNeutral && !bNeutralPressed && !bReverseCommand)
    {
        int32 GearToShift = DriveState.TargetGear;

        if (GearToShift < physx::PxVehicleGearsData::eFIRST || GearToShift > physx::PxVehicleGearsData::eSIXTH)
        {
            GearToShift = DriveState.CurrentGear;
        }

        if (bGearUpPressed && GearToShift < physx::PxVehicleGearsData::eSIXTH)
        {
            PhysicsScene->SetVehicleGear(Vehicle, GearToShift + 1, false, false);
        }
        else if (bGearDownPressed && GearToShift > physx::PxVehicleGearsData::eFIRST)
        {
            PhysicsScene->SetVehicleGear(Vehicle, GearToShift - 1, false, false);
        }
    }

    ThrottleInput = bAccelerate ? 1.0f : 0.0f;
    BrakeInput = bBrake ? 1.0f : 0.0f;
    SteeringInput = FMath::Clamp(RightInput, -1.0f, 1.0f);

    const bool bHandbrakeDown = IsKeyDown(EKeyCode::Space);

    if (bHandbrakeDown != bPreviousHandbrakeDown)
    {
        HTR_LOG(Info, "[VehicleInput] Space={}", bHandbrakeDown);
        bPreviousHandbrakeDown = bHandbrakeDown;
    }

    PhysicsScene->SetVehicleInput(Vehicle, bAccelerate, bBrake, bHandbrakeDown, RightInput < -KINDA_SMALL_NUMBER, RightInput > KINDA_SMALL_NUMBER);

    FVehicleWheelState WheelStates[4];

    if (!PhysicsScene->GetVehicleWheelStates(Vehicle, WheelStates, 4, bIsVehicleInAir))
    {
        return;
    }

    WheelContactCount = 0;

    for (int32 i = 0; i < 4; ++i)
    {
        const FVehicleWheelState& WheelState = WheelStates[i];

        WheelContactStates[i] = WheelState.bHasContact;

        FVehicleWheelTelemetry& Telemetry = WheelTelemetry[i];
        Telemetry.bIsInAir = WheelState.bIsInAir;
        Telemetry.bHasContact = WheelState.bHasContact;
        Telemetry.LongitudinalSlip = WheelState.LongitudinalSlip;
        Telemetry.LateralSlip = WheelState.LateralSlip;
        Telemetry.TireFriction = WheelState.TireFriction;
        Telemetry.SuspensionJounce = WheelState.SuspensionJounce;
        Telemetry.SuspensionTotalForce = WheelState.SuspensionTotalForce;
        Telemetry.SuspensionJounceVelocityEstimate = WheelState.SuspensionJounceVelocityEstimate;
        Telemetry.SuspensionSpringComponent = WheelState.SuspensionSpringComponent;
        Telemetry.SuspensionDamperComponent = WheelState.SuspensionDamperComponent;
        Telemetry.TireSurfaceType = WheelState.TireSurfaceType;

        if (WheelContactStates[i])
        {
            ++WheelContactCount;
        }
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

        const FVector WheelScale(WheelState.WheelRadius, WheelState.WheelRadius, WheelState.WheelWidth);

        WheelComponent->SetTransform(FTransform(WheelRotation, WheelLocation, WheelScale));
    }
}

void UVehicleMovementComponent::OnUnregister()
{
    if (VehicleHUD)
    {
        UVehicleHUD* HUD = VehicleHUD;
        VehicleHUD = nullptr;
        HUD->OnDestroyed = {};
        HUD->Destroy();
    }

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
