
#include "EnginePCH.h"
#include "UI/VehicleHUD.h"

#include "UI/TextBlock.h"
#include "Components/VehicleMovementComponent.h"
#include "Physics/PhysicalMaterial.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
    const char* GetVehicleSurfaceName(int32 SurfaceType)
    {
        switch (static_cast<EVehicleSurfaceType>(SurfaceType))
        {
        case EVehicleSurfaceType::Asphalt:
            return "ASPHALT";

        case EVehicleSurfaceType::Dirt:
            return "DIRT";

        case EVehicleSurfaceType::Ice:
            return "ICE";

        case EVehicleSurfaceType::Grass:
            return "GRASS";

        case EVehicleSurfaceType::Metal:
            return "METAL";

        default:
            return "UNKNOWN";
        }
    }
}

void UVehicleHUD::NativeConstruct()
{
    Super::NativeConstruct();

    SpeedText = GetWidget<UTextBlock>("SpeedText");
    GearText = GetWidget<UTextBlock>("GearText");
    RPMText = GetWidget<UTextBlock>("RPMText");
    InputText = GetWidget<UTextBlock>("InputText");
    WheelText = GetWidget<UTextBlock>("WheelText");

    LocalVelocityText = GetWidget<UTextBlock>("LocalVelocityText");
    LocalAngularVelocityText = GetWidget<UTextBlock>("LocalAngularVelocityText");

    WheelTelemetryTitleText = GetWidget<UTextBlock>("WheelTelemetryTitleText");

    WheelTelemetryText[0] = GetWidget<UTextBlock>("WheelFLTelemetryText");
    WheelTelemetryText[1] = GetWidget<UTextBlock>("WheelFRTelemetryText");
    WheelTelemetryText[2] = GetWidget<UTextBlock>("WheelRLTelemetryText");
    WheelTelemetryText[3] = GetWidget<UTextBlock>("WheelRRTelemetryText");
}

void UVehicleHUD::NativeTick(float DeltaTime)
{
    Super::NativeTick(DeltaTime);

    if (!VehicleMovement)
    {
        return;
    }

    // ------------------------------------------------------------
    // 기본 차량 정보
    // ------------------------------------------------------------

    if (SpeedText)
    {
        std::ostringstream Stream;
        Stream << "SPEED " << std::fixed << std::setprecision(0) << std::abs(VehicleMovement->GetVehicleSpeed()) * 3.6f << " km/h";
        SpeedText->SetText(Stream.str());
    }

    if (GearText)
    {
        const int32 Gear = VehicleMovement->GetCurrentGear();
        const std::string GearLabel = Gear == 0 ? "R" : Gear == 1 ? "N" : std::to_string(Gear - 1);

        std::ostringstream Stream;
        Stream << "GEAR " << GearLabel << " | " << (VehicleMovement->IsAutomaticTransmission() ? "AUTO" : "MANUAL");
        GearText->SetText(Stream.str());
    }

    if (RPMText)
    {
        std::ostringstream Stream;
        Stream << "RPM " << std::fixed << std::setprecision(0) << VehicleMovement->GetEngineRPM();
        RPMText->SetText(Stream.str());
    }

    if (InputText)
    {
        std::ostringstream Stream;
        Stream << std::fixed << std::setprecision(2)
            << "THROTTLE " << VehicleMovement->GetThrottleInput()
            << " | BRAKE " << VehicleMovement->GetBrakeInput()
            << " | STEER " << VehicleMovement->GetSteeringInput();

        InputText->SetText(Stream.str());
    }

    if (WheelText)
    {
        std::ostringstream Stream;
        Stream << "WHEELS "
            << "FL:" << (VehicleMovement->IsWheelInContact(0) ? "ON" : "OFF")
            << " FR:" << (VehicleMovement->IsWheelInContact(1) ? "ON" : "OFF")
            << " RL:" << (VehicleMovement->IsWheelInContact(2) ? "ON" : "OFF")
            << " RR:" << (VehicleMovement->IsWheelInContact(3) ? "ON" : "OFF")
            << " | CONTACT " << VehicleMovement->GetWheelContactCount() << "/4";

        WheelText->SetText(Stream.str());
    }

    // ------------------------------------------------------------
    // 차량 로컬 속도
    // ------------------------------------------------------------

    if (LocalVelocityText)
    {
        const FVector& Velocity = VehicleMovement->GetLocalLinearVelocity();

        std::ostringstream Stream;
        Stream << std::fixed << std::showpos << std::setprecision(3)
            << "LOCAL VELOCITY  X(FWD): " << Velocity.X
            << " | Y(RIGHT): " << Velocity.Y
            << " | Z(UP): " << Velocity.Z
            << " m/s";

        LocalVelocityText->SetText(Stream.str());
    }

    if (LocalAngularVelocityText)
    {
        const FVector& AngularVelocity = VehicleMovement->GetLocalAngularVelocity();

        std::ostringstream Stream;
        Stream << std::fixed << std::showpos << std::setprecision(3)
            << "LOCAL ANGULAR VELOCITY  X: " << AngularVelocity.X
            << " | Y: " << AngularVelocity.Y
            << " | Z: " << AngularVelocity.Z
            << " rad/s";

        LocalAngularVelocityText->SetText(Stream.str());
    }

    // ------------------------------------------------------------
    // 바퀴별 텔레메트리
    // ------------------------------------------------------------

    static const char* WheelNames[4] = { "FL", "FR", "RL", "RR" };

    for (int32 i = 0; i < 4; ++i)
    {
        if (!WheelTelemetryText[i])
        {
            continue;
        }

        const FVehicleWheelTelemetry* Telemetry = VehicleMovement->GetWheelTelemetry(i);

        if (!Telemetry)
        {
            WheelTelemetryText[i]->SetText(std::string(WheelNames[i]) + " TELEMETRY UNAVAILABLE");
            continue;
        }

        const char* ContactStatus = Telemetry->bHasContact ? "CONTACT" : (Telemetry->bIsInAir ? "AIR" : "NO CONTACT");
        const char* SurfaceName = Telemetry->bHasContact ? GetVehicleSurfaceName(Telemetry->TireSurfaceType) : "NONE";

        const float LateralSlipDegrees = FMath::RadiansToDegrees(Telemetry->LateralSlip);

        std::ostringstream Stream;
        Stream << std::fixed
            << WheelNames[i] << " " << ContactStatus
            << " | LONG " << std::showpos << std::setprecision(3) << Telemetry->LongitudinalSlip
            << " | LAT " << std::showpos << std::setprecision(1) << LateralSlipDegrees << " deg"
            << " | MU " << std::noshowpos << std::setprecision(2) << Telemetry->TireFriction
            << " | J " << std::setprecision(3) << Telemetry->SuspensionJounce << " m"
            << " | F " << std::setprecision(0) << Telemetry->SuspensionSpringForce << " N"
            << " | " << SurfaceName;

        WheelTelemetryText[i]->SetText(Stream.str());
    }
}

void UVehicleHUD::NativeDestruct()
{
    VehicleMovement = nullptr;

    SpeedText = nullptr;
    GearText = nullptr;
    RPMText = nullptr;
    InputText = nullptr;
    WheelText = nullptr;

    LocalVelocityText = nullptr;
    LocalAngularVelocityText = nullptr;
    WheelTelemetryTitleText = nullptr;

    for (int32 i = 0; i < 4; ++i)
    {
        WheelTelemetryText[i] = nullptr;
    }

    Super::NativeDestruct();
}
