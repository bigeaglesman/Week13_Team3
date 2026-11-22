#pragma once

#include "Core/Types.h"
#include <atomic>

namespace physx
{
	class PxJoint;
	class PxScene;
}

class FConstraintInstance
{
public:
	FConstraintInstance() = default;
	~FConstraintInstance();

	FConstraintInstance(const FConstraintInstance&) = delete;
	FConstraintInstance& operator=(const FConstraintInstance&) = delete;

	void Initialize(physx::PxJoint* InJoint, physx::PxScene* InScene);
	void Terminate();

	bool IsValid() const;

private:
	friend class FPhysXScene;

	void Initialize_NoLock(physx::PxJoint* InJoint, physx::PxScene* InScene);
	void Terminate_NoLock();

	physx::PxJoint* Joint = nullptr;
	std::atomic<physx::PxScene*> Scene{ nullptr };
};