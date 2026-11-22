#include "EnginePCH.h"
#include "Physics/ConstraintInstance.h"

#include <PxPhysicsAPI.h>
#include <PxSceneLock.h>

FConstraintInstance::~FConstraintInstance()
{
	Terminate();
}

void FConstraintInstance::Initialize(physx::PxJoint* InJoint, physx::PxScene* InScene)
{
	Terminate();

	if (!InJoint || !InScene)
	{
		return;
	}

	Joint = InJoint;
	Scene = InScene;
}

void FConstraintInstance::Terminate()
{
	if (!Joint)
	{
		Scene = nullptr;
		return;
	}

	if (Scene)
	{
		physx::PxSceneWriteLock SceneLock(*Scene);
		Joint->release();
	}
	else
	{
		Joint->release();
	}

	Joint = nullptr;
	Scene = nullptr;
}
