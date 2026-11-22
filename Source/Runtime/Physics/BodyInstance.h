#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"
#include <atomic>

namespace physx
{
	class PxScene;
	class PxRigidActor;
	class PxRigidDynamic;
}

class UPrimitiveComponent;
class FPhysXScene;

class FBodyInstance
{
public:
	~FBodyInstance();

	bool IsValid() const;
	bool IsDynamic() const;

	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }
	int32 GetBoneIndex() const { return BoneIndex; }
	float GetMass() const;

	FVector GetLinearVelocity() const;
	FVector GetAngularVelocity() const;

	void SetLinearVelocity(const FVector& Velocity, bool bAutoWake = true);
	void SetAngularVelocity(const FVector& Velocity, bool bAutoWake = true);

	void AddForce(const FVector& Force, bool bAutoWake = true);
	void AddImpulse(const FVector& Impulse, bool bAutoWake = true);
	void AddTorque(const FVector& Torque, bool bAutoWake = true);

	void Wake();
	void Sleep();

	void SetKinematic(bool bEnable);
	void SetGravityEnabled(bool bEnable);
private:
	friend class FPhysXScene;

	void Initialize(physx::PxRigidActor* InActor, physx::PxScene* InScene, UPrimitiveComponent* InOwnerComponent, int32 InBoneIndex = -1);
	void Terminate();

	physx::PxRigidDynamic* GetDynamicActor() const;

	std::atomic<physx::PxScene*> Scene{ nullptr };
	physx::PxRigidActor* Actor = nullptr;
	UPrimitiveComponent* OwnerComponent = nullptr;
	int32 BoneIndex = -1;
};