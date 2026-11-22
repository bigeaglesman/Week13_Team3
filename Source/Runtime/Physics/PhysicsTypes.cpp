#include "EnginePCH.h"
#include "Physics/PhysicsTypes.h"

#include <PxPhysicsAPI.h>

physx::PxFilterData FMaskFilter::ToPhysX() const
{
    physx::PxFilterData FilterData;
    FilterData.word0 = Word0;
    FilterData.word1 = Word1;
    FilterData.word2 = Word2;
    FilterData.word3 = Word3;
    return FilterData;
}

FMaskFilter FMaskFilter::FromPhysX(const physx::PxFilterData& FilterData)
{
    FMaskFilter Result;
    Result.Word0 = FilterData.word0;
    Result.Word1 = FilterData.word1;
    Result.Word2 = FilterData.word2;
    Result.Word3 = FilterData.word3;
    return Result;
}