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

	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent.load(std::memory_order_acquire); }
	int32 GetBoneIndex() const { return BoneIndex.load(std::memory_order_acquire); }
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

	bool IsStatic() const;
private:
	friend class FPhysXScene;

	void Initialize_NoLock(physx::PxRigidActor* InActor, physx::PxScene* InScene, UPrimitiveComponent* InOwnerComponent, int32 InBoneIndex = -1);
	void Terminate_NoLock();
	bool IsValid_NoLock() const;

	physx::PxRigidDynamic* GetDynamicActor_NoLock() const;

	std::atomic<physx::PxScene*> Scene{ nullptr };
	physx::PxRigidActor* Actor = nullptr;
	std::atomic<UPrimitiveComponent*> OwnerComponent{ nullptr };
	std::atomic<int32> BoneIndex{ -1 };
};