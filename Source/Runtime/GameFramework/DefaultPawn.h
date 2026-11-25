#pragma once

#include "GameFramework/Pawn.h"
#include "DefaultPawn.generated.h"

class UCameraComponent;
class UStaticMeshComponent;

UCLASS()
class ADefaultPawn : public APawn
{
	GENERATED_BODY()
public:
	ADefaultPawn();
	// MovementComponent가 없으므로 입력은 Pawn에 누적되고, 여기서 소비해 이동한다
	void Tick(float DeltaTime) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
private:
	UCameraComponent* CameraComponent = nullptr;
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	// 씬마다 이동 속도를 정할 수 있게 노출 (언리얼 UFloatingPawnMovement::MaxSpeed 역할)
	UPROPERTY(EditAnywhere)
	float MoveSpeed = 20.0f;
};
