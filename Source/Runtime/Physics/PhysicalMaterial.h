#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "PhysicalMaterial.generated.h"

namespace physx
{
	class PxMaterial;
}

UENUM()
enum class EVehicleSurfaceType : uint8
{
	Asphalt = 0,
	Dirt,
	Ice,
	Grass,
	Metal
};

inline constexpr uint32 VehicleDrivableSurfaceQueryFlag = 1u << 0;

// 표면의 마찰·반발 설정 하나. UE 4.27의 UPhysicalMaterial 자리다.
// 바디의 도형이 이 재질을 쓰면 PxMaterial을 처음 쓸 때 한 번 만들어 재사용한다.
// 값을 바꾸면 이미 만든 PxMaterial에도 바로 반영된다 (PostEditChangeProperty / SetXXX).
UCLASS()
class UPhysicalMaterial : public UObject
{
	GENERATED_BODY()
public:
	UPhysicalMaterial() = default;
	virtual ~UPhysicalMaterial();

	// 멈춰 있는 물체를 밀기 시작할 때의 마찰 계수
	UPROPERTY(EditAnywhere)
	float StaticFriction = 0.5f;

	// 미끄러지는 중의 마찰 계수
	UPROPERTY(EditAnywhere)
	float DynamicFriction = 0.5f;

	// 반발 계수. 0 = 튀지 않음, 1 = 같은 속도로 튐
	UPROPERTY(EditAnywhere)
	float Restitution = 0.1f;

	UPROPERTY(EditAnywhere)
	EVehicleSurfaceType VehicleSurfaceType = EVehicleSurfaceType::Asphalt;

	// 값이 PhysX가 받는 범위 안인지 (마찰 >= 0, 0 <= 반발 <= 1)
	bool IsValid() const;

	// 이 설정의 PxMaterial. 처음 부를 때 만든다. PhysX가 초기화되지 않았거나 값이 잘못됐으면 nullptr
	physx::PxMaterial* GetPxMaterial();

	// 값이 바뀌었으면 이미 만든 PxMaterial에 다시 넣는다.
	void UpdatePxMaterial();

	virtual void PostEditChangeProperty(const FProperty& Property) override;

private:
	physx::PxMaterial* PxMaterialHandle = nullptr;
};
