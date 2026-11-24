#pragma once

#include <PxScene.h>

// PxScene 읽기·쓰기 락. 씬을 eREQUIRE_RW_LOCK으로 만들었으므로, 씬에 들어간 액터·조인트를 건드리는 PhysX 호출은 이 락 안에서 한다.
//   읽기: getGlobalPose, getActiveActors, getRenderBuffer ...
//   쓰기: simulate/fetchResults, addActor, release, setKinematicTarget, 조인트 생성 ...
// 같은 스레드에서 읽기·쓰기 모두 다시 잡을 수 있고, 쓰기 안에서 읽기도 된다. 읽기를 잡은 채로 쓰기를 잡으면(락 승격) 안 된다.
// 발제의 SCOPED_READ_LOCK(PxSceneReadLock)과 같은 일을 하되, 씬이 nullptr이면 아무것도 하지 않는다.
// Runtime/Physics 안의 .cpp에서 <PxPhysicsAPI.h> 다음에 include한다.


class FPhysXSceneReadLock
{
public:
	FPhysXSceneReadLock(physx::PxScene* InScene, const char* File, physx::PxU32 Line)
		: Scene(InScene)
	{
		if (Scene) Scene->lockRead(File, Line);
	}
	~FPhysXSceneReadLock()
	{
		if (Scene) Scene->unlockRead();
	}
	FPhysXSceneReadLock(const FPhysXSceneReadLock&) = delete;
	FPhysXSceneReadLock& operator=(const FPhysXSceneReadLock&) = delete;

private:
	physx::PxScene* Scene;
};

class FPhysXSceneWriteLock
{
public:
	FPhysXSceneWriteLock(physx::PxScene* InScene, const char* File, physx::PxU32 Line)
		: Scene(InScene)
	{
		if (Scene) Scene->lockWrite(File, Line);
	}
	~FPhysXSceneWriteLock()
	{
		if (Scene) Scene->unlockWrite();
	}
	FPhysXSceneWriteLock(const FPhysXSceneWriteLock&) = delete;
	FPhysXSceneWriteLock& operator=(const FPhysXSceneWriteLock&) = delete;

private:
	physx::PxScene* Scene;
};

#define PHYSX_LOCK_JOIN_INNER(A, B) A##B
#define PHYSX_LOCK_JOIN(A, B) PHYSX_LOCK_JOIN_INNER(A, B)

// 이 줄부터 블록 끝까지 Scene(PxScene*)에 락을 건다.
#define SCOPED_SCENE_READ_LOCK(Scene)  FPhysXSceneReadLock  PHYSX_LOCK_JOIN(ScopedSceneReadLock_,  __LINE__)((Scene), __FILE__, __LINE__)
#define SCOPED_SCENE_WRITE_LOCK(Scene) FPhysXSceneWriteLock PHYSX_LOCK_JOIN(ScopedSceneWriteLock_, __LINE__)((Scene), __FILE__, __LINE__)
