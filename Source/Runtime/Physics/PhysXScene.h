#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"

namespace physx
{
    class PxPhysics;
    class PxScene;
    class PxRigidStatic;
    class PxRigidDynamic;
}

class USceneComponent;
struct FTransform;

class FPhysXScene
{
public:
    FPhysXScene();
    ~FPhysXScene();

    bool Initialize(physx::PxPhysics* InPhysics);
    void Shutdown();
    void ClearActors();
    void Simulate(float DeltaTime);

    void BindDynamicBody(physx::PxRigidDynamic* Body, USceneComponent* Component);
    void RemoveBodyForComponent(USceneComponent* Component);
    void SyncTransforms();

    physx::PxRigidStatic* CreateStaticBox(const FVector& Position, const FVector& HalfExtents);
    physx::PxRigidDynamic* CreateDynamicBox(const FVector& Position, const FVector& HalfExtents, float Density = 1.0f);
    physx::PxRigidDynamic* CreateDynamicBox(const FTransform& WorldTransform, const FVector& HalfExtents, float Density = 1.0f);

    physx::PxScene* GetNativeScene() const;
    bool IsInitialized() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};