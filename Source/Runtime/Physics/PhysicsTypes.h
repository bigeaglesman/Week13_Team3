#pragma once

#include "Core/Types.h"
#include "Math/Transform.h"

namespace physx
{
	class PxVec3;
	class PxQuat;
	class PxTransform;
	struct PxFilterData;
}

struct FMaskFilter
{
	static constexpr uint32 RagdollFlag = 1u << 0;

	uint32 Word0 = 0;
	uint32 Word1 = 0;
	uint32 Word2 = 0;
	uint32 Word3 = 0;

	bool HasRagdollFlag() const { return (Word0 & RagdollFlag) != 0; }
	static FMaskFilter MakeRagdoll()
	{
		FMaskFilter Filter;
		Filter.Word0 |= RagdollFlag;
		return Filter;
	}

	physx::PxFilterData ToPhysX() const;
	static FMaskFilter FromPhysX(const physx::PxFilterData& FilterData);
};

struct FPhysXTypeConverter
{
	static physx::PxVec3 ToPxVector(const FVector& Value);
	static FVector FromPxVector(const physx::PxVec3& Value);

	static physx::PxQuat ToPxQuat(const FQuat& Value);
	static FQuat FromPxQuat(const physx::PxQuat& Value);

	static physx::PxTransform ToPxTransform(const FTransform& Value);
	static FTransform FromPxTransform(const physx::PxTransform& Value, const FVector& Scale = FVector(1.0f, 1.0f, 1.0f));
};