
#include "EnginePCH.h"
#include "Components/VehicleMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Physics/PhysicsScene.h"
#include "Components/StaticMeshComponent.h"
#include "Math/Quat.h"

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

    APlayerController* Controller = GetPawnOwner() ? GetPawnOwner()->GetController() : nullptr;

    const auto IsKeyDown = [Controller](EKeyCode Key)
        {
            return Controller && Controller->IsKeyDown(static_cast<int32>(Key));
        };

    const bool bNeutralKeyDown = IsKeyDown(EKeyCode::N);
    const bool bAutomaticToggleKeyDown = IsKeyDown(EKeyCode::M);
    const bool bGearUpKeyDown = IsKeyDown(EKeyCode::E);
    const bool bGearDownKeyDown = IsKeyDown(EKeyCode::Q);

    const bool bNeutralPressed = bNeutralKeyDown && !bPreviousNeutralKeyDown;
    const bool bAutomaticTogglePressed = bAutomaticToggleKeyDown && !bPreviousAutomaticToggleKeyDown;
    const bool bGearUpPressed = bGearUpKeyDown && !bPreviousGearUpKeyDown;
    const bool bGearDownPressed = bGearDownKeyDown && !bPreviousGearDownKeyDown;

    bPreviousNeutralKeyDown = bNeutralKeyDown;
    bPreviousAutomaticToggleKeyDown = bAutomaticToggleKeyDown;
    bPreviousGearUpKeyDown = bGearUpKeyDown;
    bPreviousGearDownKeyDown = bGearDownKeyDown;

    FVehicleDriveState DriveState;

    if (!PhysicsScene->GetVehicleDriveState(Vehicle, DriveState))
    {
        return;
    }

    VehicleSpeed = DriveState.ForwardSpeed;
    EngineRPM = DriveState.EngineRPM;
    CurrentGear = DriveState.CurrentGear;

    if (bAutomaticTogglePressed)
    {
        bAutomaticTransmission = !bAutomaticTransmission;

        if (DriveState.CurrentGear >= physx::PxVehicleGearsData::eFIRST && DriveState.CurrentGear <= physx::PxVehicleGearsData::eFIFTH)
        {
            PhysicsScene->SetVehicleAutomaticGears(Vehicle, bAutomaticTransmission);
        }

        HTR_LOG(Info, "[Vehicle] Transmission Mode | Automatic={}", bAutomaticTransmission);
    }

    bool bAccelerate = false;
    bool bBrake = false;

    const bool bForwardCommand = ForwardInput > KINDA_SMALL_NUMBER;
    const bool bReverseCommand = ForwardInput < -KINDA_SMALL_NUMBER;

    const bool bVehicleInReverse =
        DriveState.CurrentGear == physx::PxVehicleGearsData::eREVERSE ||
        DriveState.TargetGear == physx::PxVehicleGearsData::eREVERSE;

    if (bNeutralPressed)
    {
        PhysicsScene->SetVehicleGear(Vehicle, physx::PxVehicleGearsData::eNEUTRAL, false, true);
        bAccelerate = false;
        bBrake = false;
    }
    else if (bForwardCommand)
    {
        const bool bMustStop = bVehicleInReverse || DriveState.CurrentGear == physx::PxVehicleGearsData::eNEUTRAL;

        if (bMustStop && std::abs(DriveState.ForwardSpeed) > 0.5f)
        {
            bBrake = true;
        }
        else
        {
            if (bVehicleInReverse || DriveState.CurrentGear == physx::PxVehicleGearsData::eNEUTRAL)
            {
                PhysicsScene->SetVehicleGear(Vehicle, physx::PxVehicleGearsData::eFIRST, bAutomaticTransmission, true);
            }

            bAccelerate = true;
        }
    }
    else if (bReverseCommand)
    {
        const bool bMustStop = !bVehicleInReverse;

        if (bMustStop && std::abs(DriveState.ForwardSpeed) > 0.5f)
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

    if (!bAutomaticTransmission && !bNeutralPressed && !bReverseCommand)
    {
        int32 GearToShift = DriveState.TargetGear;

        if (GearToShift < physx::PxVehicleGearsData::eFIRST || GearToShift > physx::PxVehicleGearsData::eFIFTH)
        {
            GearToShift = DriveState.CurrentGear;
        }

        if (bGearUpPressed && GearToShift < physx::PxVehicleGearsData::eFIFTH)
        {
            PhysicsScene->SetVehicleGear(Vehicle, GearToShift + 1, false, false);
        }
        else if (bGearDownPressed && GearToShift > physx::PxVehicleGearsData::eFIRST)
        {
            PhysicsScene->SetVehicleGear(Vehicle, GearToShift - 1, false, false);
        }
    }

    PhysicsScene->SetVehicleInput(Vehicle, bAccelerate, bBrake, IsKeyDown(EKeyCode::Space), RightInput < -KINDA_SMALL_NUMBER, RightInput > KINDA_SMALL_NUMBER);

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
