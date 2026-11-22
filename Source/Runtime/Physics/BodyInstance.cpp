#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsTypes.h"
#include "Components/PrimitiveComponent.h"

#include <PxPhysicsAPI.h>
#include <PxSceneLock.h>

FBodyInstance::~FBodyInstance()
{
    Terminate();
}

void FBodyInstance::Terminate()
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        Terminate_NoLock();
        return;
    }

    physx::PxSceneWriteLock SceneLock(*ScenePtr);

    if (Scene.load(std::memory_order_acquire) == ScenePtr)
    {
        Terminate_NoLock();
    }
}

void FBodyInstance::Initialize_NoLock(physx::PxRigidActor* InActor, physx::PxScene* InScene, UPrimitiveComponent* InOwnerComponent, int32 InBoneIndex)
{
    if (!InActor || !InScene || !InOwnerComponent)
    {
        return;
    }

    Actor = InActor;
    OwnerComponent.store(InOwnerComponent, std::memory_order_release);
    BoneIndex.store(InBoneIndex, std::memory_order_release);
    Actor->userData = this;
    Scene.store(InScene, std::memory_order_release);
}

void FBodyInstance::Terminate_NoLock()
{
    if (Actor && Actor->userData == this)
    {
        Actor->userData = nullptr;
    }

    Actor = nullptr;
    OwnerComponent.store(nullptr, std::memory_order_release);
    BoneIndex.store(-1, std::memory_order_release);
    Scene.store(nullptr, std::memory_order_release);
}

bool FBodyInstance::IsValid_NoLock() const
{
    return Actor != nullptr && Scene.load(std::memory_order_acquire) != nullptr;
}

physx::PxRigidDynamic* FBodyInstance::GetDynamicActor_NoLock() const
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
    return Scene.load(std::memory_order_acquire) == ScenePtr && IsValid_NoLock();
}

bool FBodyInstance::IsDynamic() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return false;
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    return GetDynamicActor_NoLock() != nullptr;
}

bool FBodyInstance::IsStatic() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return false;
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    return Actor && Actor->getType() == physx::PxActorType::eRIGID_STATIC;
}

float FBodyInstance::GetMass() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return 0.0f;
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    const physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock();
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
    const physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock();

    if (!Dynamic)
    {
        return FVector();
    }

    return FPhysXTypeConverter::FromPxVector(Dynamic->getLinearVelocity());
}

FVector FBodyInstance::GetAngularVelocity() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return FVector();
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    const physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock();

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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
    {
        Dynamic->setLinearVelocity(FPhysXTypeConverter::ToPxVector(Velocity), bAutoWake);
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
    {
        Dynamic->addForce(FPhysXTypeConverter::ToPxVector(Force), physx::PxForceMode::eFORCE, bAutoWake);
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
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

    if (physx::PxRigidDynamic* Dynamic = GetDynamicActor_NoLock())
    {
        Dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !bEnable);
        Dynamic->wakeUp();
    }
}