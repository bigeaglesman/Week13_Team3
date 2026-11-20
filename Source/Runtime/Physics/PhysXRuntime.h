#pragma once

#include "Core/Types.h"

namespace physx
{
	class PxPhysics;
    class PxCooking;
}

class FPhysXRuntime
{
public:
    FPhysXRuntime();
    ~FPhysXRuntime();

    bool Initialize();
    void Shutdown();

    bool IsInitialized() const;
    physx::PxPhysics* GetPhysics() const;
    physx::PxCooking* GetCooking() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};