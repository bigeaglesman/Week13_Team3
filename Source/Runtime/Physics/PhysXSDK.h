#pragma once
#include <PxPhysicsAPI.h>

// 프로세스 전역 PhysX 객체. 엔진 시작/종료 시 한 번씩 Init/Term 한다.
namespace PhysXSDK
{
	void Init();
	void Term();
	physx::PxPhysics* Get();
	physx::PxCooking* Cooking();
	physx::PxDefaultCpuDispatcher* Dispatcher();
}
