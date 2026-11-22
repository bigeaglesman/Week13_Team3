#include "EnginePCH.h"
#include "Physics/ConstraintInstance.h"

#include <PxPhysicsAPI.h>
#include <PxSceneLock.h>

FConstraintInstance::~FConstraintInstance()
{
    Terminate();
}

void FConstraintInstance::Initialize_NoLock(physx::PxJoint* InJoint, physx::PxScene* InScene)
{
    if (!InJoint || !InScene)
    {
        return;
    }

    Joint = InJoint;
    Scene.store(InScene, std::memory_order_release);
}

void FConstraintInstance::Terminate_NoLock()
{
    physx::PxJoint* JointToRelease = Joint;

    Joint = nullptr;
    Scene.store(nullptr, std::memory_order_release);

    if (JointToRelease)
    {
        JointToRelease->release();
    }
}

void FConstraintInstance::Terminate()
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (ScenePtr)
    {
        physx::PxSceneWriteLock SceneLock(*ScenePtr);

        if (Scene.load(std::memory_order_acquire) == ScenePtr)
        {
            Terminate_NoLock();
        }

        return;
    }

    Terminate_NoLock();
}

bool FConstraintInstance::IsValid() const
{
    physx::PxScene* ScenePtr = Scene.load(std::memory_order_acquire);

    if (!ScenePtr)
    {
        return false;
    }

    physx::PxSceneReadLock SceneLock(*ScenePtr);
    return Scene.load(std::memory_order_acquire) == ScenePtr && Joint != nullptr;
}