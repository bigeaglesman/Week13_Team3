#pragma once

#include "Math/Transform.h"

namespace physx
{
    class PxRigidDynamic;
    class PxVehicleDrive4W;
    class PxVehicleWheels;
}

class FPhysicsScene;

class FVehicleInstance
{
public:
    FVehicleInstance() = default;
    ~FVehicleInstance();

    FVehicleInstance(const FVehicleInstance&) = delete;
    FVehicleInstance& operator=(const FVehicleInstance&) = delete;

    bool InitVehicle(FPhysicsScene* InPhysicsScene, const FTransform& Transform, bool bNotifyRigidBodyCollision);
    void TermVehicle();
    physx::PxVehicleWheels* GetVehicle() const;

    bool IsValid() const { return ChassisActor != nullptr && Vehicle != nullptr; }

private:
    physx::PxRigidDynamic* ChassisActor = nullptr;
    physx::PxVehicleDrive4W* Vehicle = nullptr;
};
