#include "EnginePCH.h"
#include "Physics/PhysicsCore.h"

#include <PxPhysicsAPI.h>

using namespace physx;

namespace
{
	PxDefaultAllocator     GPhysXAllocator;
	PxDefaultErrorCallback GPhysXErrorCallback;
}

FPhysicsCore& FPhysicsCore::Get()
{
	static FPhysicsCore Instance;
	return Instance;
}

bool FPhysicsCore::Init()
{
	if (IsInitialized())
	{
		return true;
	}

	Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
	if (!Foundation)
	{
		return false;
	}

	// PVD가 켜져 있으면 연결한다. 꺼져 있으면 연결만 실패하고 나머지는 그대로 동작한다.
	Pvd = PxCreatePvd(*Foundation);
	PvdTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
	Pvd->connect(*PvdTransport, PxPvdInstrumentationFlag::eALL);

	const PxTolerancesScale Scale;   // 기본값이 m 단위 (length 1, speed 10)

	Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, Scale, false, Pvd);
	if (!Physics)
	{
		Shutdown();
		return false;
	}

	PxInitExtensions(*Physics, Pvd);   // 조인트, PxRigidBodyExt 등 확장 기능

	if (!PxInitVehicleSDK(*Physics))
	{
		HTR_LOG(Warning, "[PhysX] Vehicle SDK initialization failed.");
		Shutdown();
		return false;
	}

	bVehicleSDKInitialized = true;

	PxVehicleSetBasisVectors(PxVec3(0.0f, 0.0f, 1.0f), PxVec3(1.0f, 0.0f, 0.0f));

	PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);
	HTR_LOG(Info, "[PhysX] Vehicle SDK initialized. Up=(0,0,1), Forward=(1,0,0).");

	Cooking = PxCreateCooking(PX_PHYSICS_VERSION, *Foundation, PxCookingParams(Scale));
	Dispatcher = PxDefaultCpuDispatcherCreate(2);
	DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.1f);   // 정마찰, 동마찰, 반발
	return true;
}

void FPhysicsCore::Shutdown()
{
	// 만든 것의 역순. PxScene은 이미 모두 release된 상태여야 한다.
	if (DefaultMaterial)		{ DefaultMaterial->release(); DefaultMaterial = nullptr; }
	if (Dispatcher)				{ Dispatcher->release();      Dispatcher = nullptr; }
	if (Cooking)				{ Cooking->release();         Cooking = nullptr; }
	if (bVehicleSDKInitialized)	{ PxCloseVehicleSDK(); bVehicleSDKInitialized = false; }
	if (Physics)				{ PxCloseExtensions(); Physics->release(); Physics = nullptr; }
	if (Pvd)					{ Pvd->release();             Pvd = nullptr; }
	if (PvdTransport)			{ PvdTransport->release();    PvdTransport = nullptr; }
	if (Foundation)				{ Foundation->release();      Foundation = nullptr; }
}
