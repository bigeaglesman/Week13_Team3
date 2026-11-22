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

physx::PxVec3 FPhysXTypeConverter::ToPxVector(const FVector& Value)
{
    return physx::PxVec3(Value.X, Value.Y, Value.Z);
}

FVector FPhysXTypeConverter::FromPxVector(const physx::PxVec3& Value)
{
    return FVector(Value.x, Value.y, Value.z);
}

physx::PxQuat FPhysXTypeConverter::ToPxQuat(const FQuat& Value)
{
    const FQuat Rotation = Value.Normalized();
    return physx::PxQuat(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
}

FQuat FPhysXTypeConverter::FromPxQuat(const physx::PxQuat& Value)
{
    return FQuat(Value.x, Value.y, Value.z, Value.w).Normalized();
}

physx::PxTransform FPhysXTypeConverter::ToPxTransform(const FTransform& Value)
{
    return physx::PxTransform(ToPxVector(Value.Location), ToPxQuat(Value.Rotation));
}

FTransform FPhysXTypeConverter::FromPxTransform(const physx::PxTransform& Value, const FVector& Scale)
{
    return FTransform(FromPxQuat(Value.q), FromPxVector(Value.p), Scale);
}