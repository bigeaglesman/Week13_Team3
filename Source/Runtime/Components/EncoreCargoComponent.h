#pragma once

#include "Components/ActorComponent.h"
#include "EncoreCargoComponent.generated.h"

class UBoxComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UVehicleMovementComponent;
namespace physx { class PxRevoluteJoint; }

// Carries the stage-diving performer and the hinged mango carton on the encore truck.
UCLASS()
class UEncoreCargoComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEncoreCargoComponent();
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

protected:
	void OnUnregister() override;

private:
	void AttachLid();
	void LaunchMannequin();

	UVehicleMovementComponent* Movement = nullptr;
	UBoxComponent* Lid = nullptr;
	USceneComponent* Hitori = nullptr;
	USkeletalMeshComponent* Mannequin = nullptr;
	USceneComponent* Wig = nullptr;
	int32 HeadBone = -1;
	physx::PxRevoluteJoint* Hinge = nullptr;
	float LidRetryDelay = 0.0f;
	float HitoriStartZ = 0.0f;
	bool bDived = false;
};
