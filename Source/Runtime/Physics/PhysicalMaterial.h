#pragma once

#include "UObject/Object.h"
#include "PhysicalMaterial.generated.h"

namespace physx
{
	class PxPhysics;
	class PxMaterial;
}

UCLASS()
class UPhysicalMaterial : public UObject
{
	GENERATED_BODY()

public:
	float StaticFriction = 0.5f;
	float DynamicFriction = 0.5f;
	float Restitution = 0.0f;

	bool IsValid() const;
	physx::PxMaterial* CreatePhysXMaterial(physx::PxPhysics* Physics) const;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
};