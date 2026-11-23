#include "EnginePCH.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXPublic.h"

#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

namespace
{
	PxD6Motion::Enum ToPxMotion(ELinearConstraintMotion Motion)
	{
		switch (Motion)
		{
		case ELinearConstraintMotion::LCM_Free:    return PxD6Motion::eFREE;
		case ELinearConstraintMotion::LCM_Limited: return PxD6Motion::eLIMITED;
		case ELinearConstraintMotion::LCM_Locked:
		default:                                   return PxD6Motion::eLOCKED;
		}
	}

	PxD6Motion::Enum ToPxMotion(EAngularConstraintMotion Motion)
	{
		switch (Motion)
		{
		case EAngularConstraintMotion::ACM_Free:    return PxD6Motion::eFREE;
		case EAngularConstraintMotion::ACM_Limited: return PxD6Motion::eLIMITED;
		case EAngularConstraintMotion::ACM_Locked:
		default:                                    return PxD6Motion::eLOCKED;
		}
	}

	// PhysX 각도 리밋은 (0, Pi) 범위여야 유효하다.
	float DegreesToClampedRad(float Degrees)
	{
		constexpr float DegToRad = PxPi / 180.0f;
		return std::clamp(Degrees * DegToRad, 0.01f, PxPi - 0.01f);
	}

	void WakeUpIfDynamic(PxRigidActor* Actor)
	{
		if (PxRigidDynamic* Dyn = Actor ? Actor->is<PxRigidDynamic>() : nullptr)
		{
			if (!(Dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
			{
				Dyn->wakeUp();
			}
		}
	}
}

FConstraintInstance::~FConstraintInstance()
{
	TermConstraint();
}

bool FConstraintInstance::InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2)
{
	if (ConstraintData)
	{
		TermConstraint();
	}

	if (!GPhysXSDK)
	{
		HTR_LOG(Error, "[PhysX] Cannot create constraint: GPhysXSDK is null");
		return false;
	}

	PxRigidActor* Actor1 = Body1 ? Body1->GetPxRigidActor() : nullptr;
	PxRigidActor* Actor2 = Body2 ? Body2->GetPxRigidActor() : nullptr;

	// 둘 다 nullptr이거나 같은 액터끼리는 조인트를 만들 수 없다.
	if ((!Actor1 && !Actor2) || (Actor1 == Actor2))
	{
		HTR_LOG(Error, "[PhysX] InitConstraint requires at least one valid actor and distinct actors");
		return false;
	}

	ConstraintData = PxD6JointCreate(
		*GPhysXSDK,
		Actor1, U2PTransform(RefFrame1),
		Actor2, U2PTransform(RefFrame2));

	if (!ConstraintData)
	{
		HTR_LOG(Error, "[PhysX] PxD6JointCreate failed");
		return false;
	}

	// 연결된 두 바디 간 자기 충돌 제어 및 PVD 시각화 활성화
	ConstraintData->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, !bDisableCollision);
	ConstraintData->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);
	ConstraintData->setConstraintFlag(PxConstraintFlag::eIMPROVED_SLERP, true);

	UpdateLinearLimit();
	UpdateAngularLimit();
	UpdateAngularDrive();

	WakeUpIfDynamic(Actor1);
	WakeUpIfDynamic(Actor2);

	return true;
}

void FConstraintInstance::TermConstraint()
{
	if (ConstraintData)
	{
		ConstraintData->release();
		ConstraintData = nullptr;
	}
}

void FConstraintInstance::UpdateLinearLimit()
{
	if (!ConstraintData)
	{
		return;
	}

	ConstraintData->setMotion(PxD6Axis::eX, ToPxMotion(XMotion));
	ConstraintData->setMotion(PxD6Axis::eY, ToPxMotion(YMotion));
	ConstraintData->setMotion(PxD6Axis::eZ, ToPxMotion(ZMotion));

	const bool bAnyLimited =
		XMotion == ELinearConstraintMotion::LCM_Limited ||
		YMotion == ELinearConstraintMotion::LCM_Limited ||
		ZMotion == ELinearConstraintMotion::LCM_Limited;

	if (bAnyLimited)
	{
		ConstraintData->setDistanceLimit(PxJointLinearLimit(std::max(LinearLimitSize, 0.001f)));
	}
}

void FConstraintInstance::UpdateAngularLimit()
{
	if (!ConstraintData)
	{
		return;
	}

	ConstraintData->setMotion(PxD6Axis::eTWIST, ToPxMotion(TwistMotion));
	ConstraintData->setMotion(PxD6Axis::eSWING1, ToPxMotion(Swing1Motion));
	ConstraintData->setMotion(PxD6Axis::eSWING2, ToPxMotion(Swing2Motion));

	// Soft angular limit: Hard limit은 바닥 접촉과 충돌 시 심한 떨림(jitter)을 유발하므로
	// 언리얼의 bSoftConstraint처럼 스프링-감쇠(PxSpring)로 부드럽고 단단하게 제한한다.
	const PxSpring LimitSpring(100.0f, 15.0f);

	if (TwistMotion == EAngularConstraintMotion::ACM_Limited)
	{
		const float TwistRad = DegreesToClampedRad(TwistLimitDegrees);
		ConstraintData->setTwistLimit(PxJointAngularLimitPair(-TwistRad, TwistRad, LimitSpring));
	}

	if (Swing1Motion == EAngularConstraintMotion::ACM_Limited ||
		Swing2Motion == EAngularConstraintMotion::ACM_Limited)
	{
		const float Swing1Rad = DegreesToClampedRad(Swing1LimitDegrees);
		const float Swing2Rad = DegreesToClampedRad(Swing2LimitDegrees);
		ConstraintData->setSwingLimit(PxJointLimitCone(Swing1Rad, Swing2Rad, LimitSpring));
	}
}

void FConstraintInstance::UpdateAngularDrive()
{
	if (!ConstraintData)
	{
		return;
	}

	const float Stiffness = std::max(AngularStiffness, 0.0f);
	const float Damping = std::max(AngularDamping, 0.0f);

	// isAcceleration = true: 질량에 무관하게 일정한 각감쇠/복원 가속도를 적용한다.
	PxD6JointDrive SlerpDrive(Stiffness, Damping, PX_MAX_F32, true);
	ConstraintData->setDrive(PxD6Drive::eSLERP, SlerpDrive);
	ConstraintData->setDrivePosition(PxTransform(PxIdentity));
	ConstraintData->setDriveVelocity(PxVec3(0.0f), PxVec3(0.0f));
}

void FConstraintInstance::SetRefFrame(int32 FrameIndex, const FTransform& InRefFrame)
{
	if (FrameIndex == 0)
	{
		RefFrame1 = InRefFrame;
		if (ConstraintData)
		{
			ConstraintData->setLocalPose(PxJointActorIndex::eACTOR0, U2PTransform(RefFrame1));
		}
	}
	else if (FrameIndex == 1)
	{
		RefFrame2 = InRefFrame;
		if (ConstraintData)
		{
			ConstraintData->setLocalPose(PxJointActorIndex::eACTOR1, U2PTransform(RefFrame2));
		}
	}
}