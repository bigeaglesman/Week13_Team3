#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"

namespace physx
{
    class PxPhysics;
    class PxScene;
    class PxRigidActor;
    class PxRigidStatic;
    class PxRigidDynamic;
}

class UPrimitiveComponent;
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

    void BindBody(physx::PxRigidStatic* Body, UPrimitiveComponent* Component);
    void BindBody(physx::PxRigidDynamic* Body, UPrimitiveComponent* Component);
    void RemoveBodyForComponent(UPrimitiveComponent* Component);
    void SyncTransforms();
    void DispatchQueuedEvents();

    physx::PxRigidStatic* CreateStaticBox(const FVector& Position, const FVector& HalfExtents);
    physx::PxRigidDynamic* CreateDynamicBox(const FVector& Position, const FVector& HalfExtents, float Density = 1.0f);
    physx::PxRigidDynamic* CreateDynamicBox(const FTransform& WorldTransform, const FVector& HalfExtents, float Density = 1.0f);

    physx::PxScene* GetNativeScene() const;
    bool IsInitialized() const;

private:
    bool BindBodyInternal(physx::PxRigidActor* Body, UPrimitiveComponent* Component);

    struct FImpl;
    TUniquePtr<FImpl> Impl;
};