#pragma once

#include "Core/Types.h"

namespace physx
{
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