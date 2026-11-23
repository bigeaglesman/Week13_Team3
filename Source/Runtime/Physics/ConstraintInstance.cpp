#include "EnginePCH.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/ConstraintSetup.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>
#include "Physics/PhysXSceneLock.h"

using namespace physx;

namespace
{
	// 프레임 위치는 메시 원본 단위라 컴포넌트 스케일을 곱한다. 바디 포즈에는 스케일이 없으므로 회전은 그대로 쓴다.
	PxTransform ToJointFrame(const FTransform& Frame, float Scale)
	{
		return PxTransform(ToPx(Frame.Location * Scale), ToPx(Frame.Rotation).getNormalized());
	}

	PxD6Motion::Enum ToPxMotion(EConstraintMotion Motion)
	{
		switch (Motion)
		{
		case EConstraintMotion::Free:    return PxD6Motion::eFREE;
		case EConstraintMotion::Limited: return PxD6Motion::eLIMITED;
		default:                         return PxD6Motion::eLOCKED;
		}
	}

	// 회전 축: Limited인데 한계가 0이면 잠근다 (자동 생성한 래그돌이 축을 잠그는 방법).
	PxD6Motion::Enum ToPxAngularMotion(EConstraintMotion Motion, float LimitDeg)
	{
		if (Motion == EConstraintMotion::Limited && LimitDeg <= 0.0f)
		{
			return PxD6Motion::eLOCKED;
		}
		return ToPxMotion(Motion);
	}

	// PhysX는 0도나 180도 이상의 한계를 받지 않는다. 잠근 축도 한계 값 자체는 유효해야 한다.
	float LimitToRadians(float LimitDeg)
	{
		return std::clamp(LimitDeg, 1.0f, 179.0f) * (PxPi / 180.0f);
	}
}

void FConstraintInstance::InitConstraint(const FConstraintSetup& Setup, FBodyInstance* ChildBody, FBodyInstance* ParentBody, float Scale)
{
	if (Joint)
	{
		return;
	}

	// 한쪽이 없으면 월드에 고정한다. PhysX는 nullptr 액터를 월드로 다루고, 그쪽 프레임을 월드 좌표로 읽는다.
	PxRigidActor* ChildActor  = ChildBody ? ChildBody->GetPxRigidActor() : nullptr;
	PxRigidActor* ParentActor = ParentBody ? ParentBody->GetPxRigidActor() : nullptr;
	if (!ChildActor && !ParentActor)
	{
		return;
	}
	if ((ChildBody && !ChildActor) || (ParentBody && !ParentActor))
	{
		return;   // 바디를 넘겼는데 PhysX 액터가 아직 없다
	}

	// 액터가 이미 씬에 있으므로 조인트를 만들고 설정하는 동안 씬에 쓰기 락을 건다.
	PxScene* Scene = ParentActor ? ParentActor->getScene() : ChildActor->getScene();
	SCOPED_SCENE_WRITE_LOCK(Scene);

	// 월드에 고정한 쪽 프레임은 월드 좌표라 스케일을 곱하지 않는다.
	const float ParentScale = ParentActor ? Scale : 1.0f;
	const float ChildScale  = ChildActor ? Scale : 1.0f;

	// UE와 같이 부모가 actor0, 자식이 actor1. Twist·Swing은 부모 프레임 기준으로 잰 자식 프레임의 회전이다.
	// 시작 포즈에서 두 프레임이 겹치도록 설계도를 만들었으므로, 처음엔 모든 각도가 0에서 시작한다.
	Joint = PxD6JointCreate(*FPhysicsCore::Get().GetPhysics(),
		ParentActor, ToJointFrame(Setup.ParentFrame, ParentScale),
		ChildActor,  ToJointFrame(Setup.ChildFrame, ChildScale));
	if (!Joint)
	{
		return;
	}

	// ---- 이동 3축 ----
	Joint->setMotion(PxD6Axis::eX, ToPxMotion(Setup.LinearXMotion));
	Joint->setMotion(PxD6Axis::eY, ToPxMotion(Setup.LinearYMotion));
	Joint->setMotion(PxD6Axis::eZ, ToPxMotion(Setup.LinearZMotion));
	if (Setup.LinearLimit > 0.0f)
	{
		Joint->setLinearLimit(PxJointLinearLimit(FPhysicsCore::Get().GetPhysics()->getTolerancesScale(), Setup.LinearLimit * Scale));
	}

	// ---- 회전 3축 ----
	Joint->setMotion(PxD6Axis::eTWIST,  ToPxAngularMotion(Setup.TwistMotion,  Setup.TwistLimitDeg));
	Joint->setMotion(PxD6Axis::eSWING1, ToPxAngularMotion(Setup.Swing1Motion, Setup.Swing1LimitDeg));
	Joint->setMotion(PxD6Axis::eSWING2, ToPxAngularMotion(Setup.Swing2Motion, Setup.Swing2LimitDeg));

	const float Twist = LimitToRadians(Setup.TwistLimitDeg);
	Joint->setTwistLimit(PxJointAngularLimitPair(-Twist, Twist));
	Joint->setSwingLimit(PxJointLimitCone(LimitToRadians(Setup.Swing1LimitDeg), LimitToRadians(Setup.Swing2LimitDeg)));

	// ---- 드라이브: 시작 포즈(프레임이 겹친 상태) 쪽으로 회전을 당기는 스프링·감쇠 ----
	if (Setup.AngularDriveStiffness > 0.0f || Setup.AngularDriveDamping > 0.0f)
	{
		// isAcceleration = true: 질량과 상관없이 같은 느낌으로 움직인다 (UE의 Acceleration Drive)
		Joint->setDrive(PxD6Drive::eSLERP, PxD6JointDrive(Setup.AngularDriveStiffness, Setup.AngularDriveDamping, PX_MAX_F32, true));
		Joint->setDrivePosition(PxTransform(PxIdentity));
	}

	// 이어진 두 바디끼리 충돌. PhysX 조인트 기본값은 충돌하지 않음이다.
	Joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, !Setup.bDisableCollision);

	// Collision 쇼 플래그에서 관절 프레임과 한계를 그린다.
	Joint->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);
}

void FConstraintInstance::InitConstraintAtPivot(const FConstraintSetup& Setup, FBodyInstance* ChildBody, FBodyInstance* ParentBody, const FTransform& PivotWorld)
{
	// 바디 포즈에는 스케일이 없으므로 강체 역변환(위치·회전)만으로 피벗을 바디 로컬로 옮긴다.
	// 바디가 없는 쪽(월드 고정)은 피벗 그대로 월드 좌표다.
	const PxTransform Pivot = ToPx(PivotWorld);
	auto ToLocalFrame = [&Pivot](FBodyInstance* Body) -> FTransform
	{
		if (!Body || !Body->IsValidBodyInstance())
		{
			return ToFTransform(Pivot);
		}
		const PxTransform BodyPose = ToPx(Body->GetUnrealWorldTransform());
		return ToFTransform(BodyPose.getInverse() * Pivot);
	};

	FConstraintSetup Framed = Setup;
	Framed.ParentFrame = ToLocalFrame(ParentBody);
	Framed.ChildFrame  = ToLocalFrame(ChildBody);
	InitConstraint(Framed, ChildBody, ParentBody, 1.0f);
}

void FConstraintInstance::TermConstraint()
{
	if (Joint)
	{
		// 씬이 먼저 해제됐으면 getScene()이 nullptr이라 락 없이 지운다.
		SCOPED_SCENE_WRITE_LOCK(Joint->getScene());
		Joint->release();
		Joint = nullptr;
	}
}
