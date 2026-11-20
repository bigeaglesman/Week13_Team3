#include "EnginePCH.h"
#include "Physics/PhysXRuntime.h"

#include <PxPhysicsAPI.h>

struct FPhysXRuntime::FImpl
{
    physx::PxDefaultAllocator Allocator;
    physx::PxDefaultErrorCallback ErrorCallback;

    physx::PxFoundation* Foundation = nullptr;
    physx::PxPhysics* Physics = nullptr;

    bool bExtensionsInitialized = false;
};

FPhysXRuntime::FPhysXRuntime()
    : Impl(MakeUnique<FImpl>())
{
}

FPhysXRuntime::~FPhysXRuntime()
{
    Shutdown();
}

bool FPhysXRuntime::Initialize()
{
    if (IsInitialized())
        return true;

    Impl->Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Impl->Allocator, Impl->ErrorCallback);

    if (!Impl->Foundation)
    {
        HTR_LOG(Error, "[PhysX] Failed to create Foundation.");
        return false;
    }

    Impl->Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Impl->Foundation, physx::PxTolerancesScale());

    if (!Impl->Physics)
    {
        HTR_LOG(Error, "[PhysX] Failed to create Physics.");
        Shutdown();
        return false;
    }

    if (!PxInitExtensions(*Impl->Physics, nullptr))
    {
        HTR_LOG(Error, "[PhysX] Failed to initialize Extensions.");
        Shutdown();
        return false;
    }

    Impl->bExtensionsInitialized = true;

    HTR_LOG(Info, "[PhysX] Runtime initialized. Version {}.{}.{}", PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX);

    return true;
}

void FPhysXRuntime::Shutdown()
{
    if (!Impl)
        return;

    if (Impl->bExtensionsInitialized)
    {
        PxCloseExtensions();
        Impl->bExtensionsInitialized = false;
    }

    if (Impl->Physics)
    {
        Impl->Physics->release();
        Impl->Physics = nullptr;
    }

    if (Impl->Foundation)
    {
        Impl->Foundation->release();
        Impl->Foundation = nullptr;
    }
}

bool FPhysXRuntime::IsInitialized() const
{
    return Impl && Impl->Foundation && Impl->Physics && Impl->bExtensionsInitialized;
}

physx::PxPhysics* FPhysXRuntime::GetPhysics() const
{
    return Impl ? Impl->Physics : nullptr;
}