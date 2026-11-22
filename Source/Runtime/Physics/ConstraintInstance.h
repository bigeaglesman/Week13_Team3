#pragma once

#include "Core/Types.h"

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

	bool IsValid() const { return Joint != nullptr; }
	physx::PxJoint* GetNativeJoint() const { return Joint; }
private:
	physx::PxJoint* Joint = nullptr;
	physx::PxScene* Scene = nullptr;
};