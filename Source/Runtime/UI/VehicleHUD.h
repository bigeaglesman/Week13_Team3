#pragma once

#include "UI/UserWidget.h"
#include "VehicleHUD.generated.h"

class UTextBlock;
class UVehicleMovementComponent;

UCLASS()
class UVehicleHUD : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetVehicleMovementComponent(UVehicleMovementComponent* InMovementComponent) { VehicleMovement = InMovementComponent; }

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(float DeltaTime) override;
    virtual void NativeDestruct() override;

private:
    UVehicleMovementComponent* VehicleMovement = nullptr;

    UTextBlock* SpeedText = nullptr;
    UTextBlock* GearText = nullptr;
    UTextBlock* RPMText = nullptr;
    UTextBlock* InputText = nullptr;
    UTextBlock* WheelText = nullptr;
};