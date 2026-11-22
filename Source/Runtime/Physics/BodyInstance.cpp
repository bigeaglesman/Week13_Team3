#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Components/PrimitiveComponent.h"

#include <PxPhysicsAPI.h>
#include <PxSceneLock.h>

FBodyInstance::~FBodyInstance()
{
    Terminate();
}

void FBodyInstance::Initialize(physx::PxRigidActor* InActor, physx::PxScene* InScene, UPrimitiveComponent* InOwnerComponent, int32 InBoneIndex)
{
    if (!InActor || !InScene || !InOwnerComponent)
    {
        return;
    }

    Actor = InActor;
    OwnerComponent = InOwnerComponent;
    BoneIndex = InBoneIndex;
    Actor->userData = this;
    Scene.store(InScene, std::memory_order_release);
}

void FBodyInstance::Terminate()
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (ScenePtr)
    {
        physx::PxSceneWriteLock SceneLock(*ScenePtr);

        if (Actor && Actor->userData == this)
        {
            Actor->userData = nullptr;
        }

        Actor = nullptr;
        OwnerComponent = nullptr;
        BoneIndex = -1;
        Scene.store(nullptr, std::memory_order_release);
        return;
    }

    Actor = nullptr;
    OwnerComponent = nullptr;
    BoneIndex = -1;
}

physx::PxRigidDynamic* FBodyInstance::GetDynamicActor() const
{
    if (!Actor || Actor->getType() != physx::PxActorType::eRIGID_DYNAMIC)
    {
        return nullptr;
    }

    return static_cast<physx::PxRigidDynamic*>(Actor);
}

bool FBodyInstance::IsValid() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return false;
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    return Scene.load(std::memory_order_acquire) == ScenePtr && Actor != nullptr;
}

bool FBodyInstance::IsDynamic() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return false;
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    return GetDynamicActor() != nullptr;
}

float FBodyInstance::GetMass() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return 0.0f;
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    const physx::PxRigidDynamic* Dynamic = GetDynamicActor();
    return Dynamic ? Dynamic->getMass() : 0.0f;
}

FVector FBodyInstance::GetLinearVelocity() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return FVector();
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    const physx::PxRigidDynamic* Dynamic = GetDynamicActor();

    if (!Dynamic)
    {
        return FVector();
    }

    const physx::PxVec3 Velocity = Dynamic->getLinearVelocity();
    return FVector(Velocity.x, Velocity.y, Velocity.z);
}

FVector FBodyInstance::GetAngularVelocity() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return FVector();
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    const physx::PxRigidDynamic* Dynamic = GetDynamicActor();

    if (!Dynamic)
    {
        return FVector();
    }

    const physx::PxVec3 Velocity = Dynamic->getAngularVelocity();
    return FVector(Velocity.x, Velocity.y, Velocity.z);
}

void FBodyInstance::SetLinearVelocity(const FVector& Velocity, bool bAutoWake)
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->setLinearVelocity(physx::PxVec3(Velocity.X, Velocity.Y, Velocity.Z), bAutoWake);
    }
}

void FBodyInstance::SetAngularVelocity(const FVector& Velocity, bool bAutoWake)
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->setAngularVelocity(physx::PxVec3(Velocity.X, Velocity.Y, Velocity.Z), bAutoWake);
    }
}

void FBodyInstance::AddForce(const FVector& Force, bool bAutoWake)
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->addForce(physx::PxVec3(Force.X, Force.Y, Force.Z), physx::PxForceMode::eFORCE, bAutoWake);
    }
}

void FBodyInstance::AddImpulse(const FVector& Impulse, bool bAutoWake)
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->addForce(physx::PxVec3(Impulse.X, Impulse.Y, Impulse.Z), physx::PxForceMode::eIMPULSE, bAutoWake);
    }
}

void FBodyInstance::AddTorque(const FVector& Torque, bool bAutoWake)
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->addTorque(physx::PxVec3(Torque.X, Torque.Y, Torque.Z), physx::PxForceMode::eFORCE, bAutoWake);
    }
}

void FBodyInstance::Wake()
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->wakeUp();
    }
}

void FBodyInstance::Sleep()
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->putToSleep();
    }
}

void FBodyInstance::SetKinematic(bool bEnable)
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bEnable);
        Dynamic->wakeUp();
    }
}

void FBodyInstance::SetGravityEnabled(bool bEnable)
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
    {
        Dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !bEnable);
        Dynamic->wakeUp();
    }
}