
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

    // 기본 차량 정보
    UTextBlock* SpeedText = nullptr;
    UTextBlock* GearText = nullptr;
    UTextBlock* RPMText = nullptr;
    UTextBlock* InputText = nullptr;
    UTextBlock* WheelText = nullptr;

    // 차량 로컬 속도 정보
    UTextBlock* LocalVelocityText = nullptr;
    UTextBlock* LocalAngularVelocityText = nullptr;

    // 바퀴별 텔레메트리
    UTextBlock* WheelTelemetryTitleText = nullptr;
    UTextBlock* WheelTelemetryText[4] = {};

    UTextBlock* DriveModeText = nullptr;
};
