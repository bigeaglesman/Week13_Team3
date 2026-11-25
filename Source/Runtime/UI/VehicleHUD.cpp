#include "EnginePCH.h"
#include "UI/VehicleHUD.h"

#include "UI/TextBlock.h"
#include "Components/VehicleMovementComponent.h"

#include <cmath>
#include <iomanip>
#include <sstream>

void UVehicleHUD::NativeConstruct()
{
    Super::NativeConstruct();

    SpeedText = GetWidget<UTextBlock>("SpeedText");
    GearText = GetWidget<UTextBlock>("GearText");
    RPMText = GetWidget<UTextBlock>("RPMText");
    InputText = GetWidget<UTextBlock>("InputText");
    WheelText = GetWidget<UTextBlock>("WheelText");
    DriveModeText = GetWidget<UTextBlock>("DriveModeText");
}

void UVehicleHUD::NativeTick(float DeltaTime)
{
    Super::NativeTick(DeltaTime);

    if (!VehicleMovement)
    {
        return;
    }

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

    if (DriveModeText)
    {
        static const char* DriveModeNames[3] = { "AWD + LSD", "AWD + OPEN", "RWD + LSD" };

        const int32 ModeIndex = VehicleMovement->GetDriveModeIndex();
        const int32 SafeModeIndex = ModeIndex >= 0 && ModeIndex < 3 ? ModeIndex : 0;

        DriveModeText->SetText(std::string("DRIVE MODE [V]  ") + DriveModeNames[SafeModeIndex]);
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
    DriveModeText = nullptr;

    Super::NativeDestruct();
}