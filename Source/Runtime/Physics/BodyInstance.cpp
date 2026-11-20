#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Components/PrimitiveComponent.h"

#include <PxPhysicsAPI.h>

FBodyInstance::~FBodyInstance()
{
	Terminate();
}

void FBodyInstance::Initialize(physx::PxRigidActor* InActor, UPrimitiveComponent* InOwnerComponent, int32 InBoneIndex)
{
	if (!InActor || !InOwnerComponent)
	{
		return;
	}

	Actor = InActor;
	OwnerComponent = InOwnerComponent;
	BoneIndex = InBoneIndex;
	Actor->userData = this;
}

void FBodyInstance::Terminate()
{
	if (Actor && Actor->userData == this)
	{
		Actor->userData = nullptr;
	}

	Actor = nullptr;
	OwnerComponent = nullptr;
	BoneIndex = -1;
}

physx::PxRigidDynamic* FBodyInstance::GetDynamicActor() const
{
	if (!Actor || Actor->getType() != physx::PxActorType::eRIGID_DYNAMIC)
	{
		return nullptr;
	}

	return static_cast<physx::PxRigidDynamic*>(Actor);
}

bool FBodyInstance::IsDynamic() const
{
	return GetDynamicActor() != nullptr;
}

float FBodyInstance::GetMass() const
{
	const physx::PxRigidDynamic* Dynamic = GetDynamicActor();
	return Dynamic ? Dynamic->getMass() : 0.0f;
}

FVector FBodyInstance::GetLinearVelocity() const
{
	const physx::PxRigidDynamic* Dynamic = GetDynamicActor();

	if (!Dynamic)
	{
		return FVector();
	}

	const physx::PxVec3 Velocity = Dynamic->getLinearVelocity();
	return FVector(Velocity.x, Velocity.y, Velocity.z);
}

FVector FBodyInstance::GetAngularVelocity() const
{
	const physx::PxRigidDynamic* Dynamic = GetDynamicActor();

	if (!Dynamic)
	{
		return FVector();
	}

	const physx::PxVec3 Velocity = Dynamic->getAngularVelocity();
	return FVector(Velocity.x, Velocity.y, Velocity.z);
}

void FBodyInstance::SetLinearVelocity(const FVector& Velocity, bool bAutoWake)
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
	{
		Dynamic->setLinearVelocity(physx::PxVec3(Velocity.X, Velocity.Y, Velocity.Z), bAutoWake);
	}
}

void FBodyInstance::SetAngularVelocity(const FVector& Velocity, bool bAutoWake)
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
	{
		Dynamic->setAngularVelocity(physx::PxVec3(Velocity.X, Velocity.Y, Velocity.Z), bAutoWake);
	}
}

void FBodyInstance::AddForce(const FVector& Force, bool bAutoWake)
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
	{
		Dynamic->addForce(physx::PxVec3(Force.X, Force.Y, Force.Z), physx::PxForceMode::eFORCE, bAutoWake);
	}
}

void FBodyInstance::AddImpulse(const FVector& Impulse, bool bAutoWake)
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
		Dynamic->addForce(physx::PxVec3(Impulse.X, Impulse.Y, Impulse.Z), physx::PxForceMode::eIMPULSE, bAutoWake);
}

void FBodyInstance::AddTorque(const FVector& Torque, bool bAutoWake)
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
		Dynamic->addTorque(physx::PxVec3(Torque.X, Torque.Y, Torque.Z), physx::PxForceMode::eFORCE, bAutoWake);
}

void FBodyInstance::Wake()
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
	{
		Dynamic->wakeUp();
	}
}

void FBodyInstance::Sleep()
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
		Dynamic->putToSleep();
}

void FBodyInstance::SetKinematic(bool bEnable)
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
	{
		Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bEnable);
		Dynamic->wakeUp();
	}
}

void FBodyInstance::SetGravityEnabled(bool bEnable)
{
	if (physx::PxRigidDynamic* Dynamic = GetDynamicActor())
	{
		Dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !bEnable);
		Dynamic->wakeUp();
	}
}