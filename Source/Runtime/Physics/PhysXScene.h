#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"

namespace physx
{
    class PxPhysics;
    class PxCooking;
    class PxScene;
    class PxRigidActor;
    class PxRigidStatic;
    class PxRigidDynamic;
}

class UPrimitiveComponent;
class USkeletalMeshComponent;
struct FTransform;
struct FStaticMeshData;

class FPhysXScene
{
public:
    FPhysXScene();
    ~FPhysXScene();

    bool Initialize(physx::PxPhysics* InPhysics, physx::PxCooking* InCooking);
    void Shutdown();
    void ClearActors();
    void Simulate(float DeltaTime);

    void BindBody(physx::PxRigidStatic* Body, UPrimitiveComponent* Component);
    void BindBody(physx::PxRigidDynamic* Body, UPrimitiveComponent* Component);
    bool BindBody(physx::PxRigidDynamic* Body, USkeletalMeshComponent* Component, int32 BoneIndex);

    void RemoveBodyForComponent(UPrimitiveComponent* Component);
    void SyncTransforms();
    void DispatchQueuedEvents();

    physx::PxRigidStatic* CreateStaticBox(const FVector& Position, const FVector& HalfExtents);
    physx::PxRigidDynamic* CreateDynamicBox(const FVector& Position, const FVector& HalfExtents, float Density = 1.0f);
    physx::PxRigidDynamic* CreateDynamicBox(const FTransform& WorldTransform, const FVector& HalfExtents, float Density = 1.0f);
    physx::PxRigidStatic* CreateStaticTriangleMesh(const FStaticMeshData& MeshData, const FTransform& WorldTransform);
    physx::PxRigidDynamic* CreateDynamicConvexMesh(const FStaticMeshData& MeshData, const FTransform& WorldTransform, float Density = 1.0f);

    physx::PxScene* GetNativeScene() const;
    bool IsInitialized() const;

private:
    bool BindBodyInternal(physx::PxRigidActor* Body, UPrimitiveComponent* Component);

    struct FImpl;
    TUniquePtr<FImpl> Impl;
};