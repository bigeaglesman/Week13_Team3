#include "EnginePCH.h"
#include "Physics/PhysicalMaterial.h"

#include "Serialization/StructuredArchiveSlots.h"

#include <PxPhysicsAPI.h>
#include <cmath>

bool UPhysicalMaterial::IsValid() const
{
	return std::isfinite(StaticFriction) && StaticFriction >= 0.0f &&
		std::isfinite(DynamicFriction) && DynamicFriction >= 0.0f &&
		std::isfinite(Restitution) && Restitution >= 0.0f && Restitution <= 1.0f;
}

physx::PxMaterial* UPhysicalMaterial::CreatePhysXMaterial(physx::PxPhysics* Physics) const
{
	if (!Physics || !IsValid())
	{
		return nullptr;
	}

	return Physics->createMaterial(StaticFriction, DynamicFriction, Restitution);
}

void UPhysicalMaterial::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	Record << SA_VALUE("StaticFriction", StaticFriction);
	Record << SA_VALUE("DynamicFriction", DynamicFriction);
	Record << SA_VALUE("Restitution", Restitution);
}
