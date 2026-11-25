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

    // PVD는 디버깅용이므로 연결에 실패해도 물리 엔진은 실행한다.
    Pvd = PxCreatePvd(*Foundation);

    if (Pvd)
    {
        PvdTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);

        if (PvdTransport)
        {
            if (!Pvd->connect(*PvdTransport, PxPvdInstrumentationFlag::eALL))
            {
                HTR_LOG(Warning, "[PhysX] PVD connection failed. Continue without PVD.");
            }
        }
        else
        {
            HTR_LOG(Warning, "[PhysX] PVD transport creation failed. Continue without PVD.");
        }
    }
    else
    {
        HTR_LOG(Warning, "[PhysX] PVD creation failed. Continue without PVD.");
    }

    const PxTolerancesScale Scale;

    Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, Scale, false, Pvd);

    if (!Physics)
    {
        Shutdown();
        return false;
    }

    if (!PxInitExtensions(*Physics, Pvd))
    {
        HTR_LOG(Error, "[PhysX] Extensions initialization failed.");
        Shutdown();
        return false;
    }

    bExtensionsInitialized = true;

    if (!PxInitVehicleSDK(*Physics))
    {
        HTR_LOG(Error, "[PhysX] Vehicle SDK initialization failed.");
        Shutdown();
        return false;
    }

    bVehicleSDKInitialized = true;

    PxVehicleSetBasisVectors(PxVec3(0.0f, 0.0f, 1.0f), PxVec3(1.0f, 0.0f, 0.0f));
    PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);

    HTR_LOG(Info, "[PhysX] Vehicle SDK initialized. Up=(0,0,1), Forward=(1,0,0).");

    Cooking = PxCreateCooking(PX_PHYSICS_VERSION, *Foundation, PxCookingParams(Scale));
    Dispatcher = PxDefaultCpuDispatcherCreate(2);
    DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.1f);

    if (!Cooking || !Dispatcher || !DefaultMaterial)
    {
        HTR_LOG(Error, "[PhysX] Required physics resource creation failed.");
        Shutdown();
        return false;
    }

    return true;
}

void FPhysicsCore::Shutdown()
{
    // 모든 월드와 PxScene이 먼저 해제된 상태여야 한다.
    if (DefaultMaterial)
    {
        DefaultMaterial->release();
        DefaultMaterial = nullptr;
    }

    if (Dispatcher)
    {
        Dispatcher->release();
        Dispatcher = nullptr;
    }

    if (Cooking)
    {
        Cooking->release();
        Cooking = nullptr;
    }

    if (bVehicleSDKInitialized)
    {
        PxCloseVehicleSDK();
        bVehicleSDKInitialized = false;
    }

    if (bExtensionsInitialized)
    {
        PxCloseExtensions();
        bExtensionsInitialized = false;
    }

    if (Physics)
    {
        Physics->release();
        Physics = nullptr;
    }

    if (Pvd)
    {
        Pvd->disconnect();
        Pvd->release();
        Pvd = nullptr;
    }

    if (PvdTransport)
    {
        PvdTransport->release();
        PvdTransport = nullptr;
    }

    if (Foundation)
    {
        Foundation->release();
        Foundation = nullptr;
    }
}