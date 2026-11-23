#include "EnginePCH.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/ConstraintSetup.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>

using namespace physx;

namespace
{
	// 프레임 위치는 메시 원본 단위라 컴포넌트 스케일을 곱한다. 바디 포즈에는 스케일이 없으므로 회전은 그대로 쓴다.
	PxTransform ToJointFrame(const FTransform& Frame, float Scale)
	{
		return PxTransform(ToPx(Frame.Location * Scale), ToPx(Frame.Rotation).getNormalized());
	}

	// 한계가 0이면 그 축을 잠그고, 아니면 한계 안에서 풀어 준다.
	void SetAngularMotion(PxD6Joint& Joint, PxD6Axis::Enum Axis, float LimitDeg)
	{
		Joint.setMotion(Axis, LimitDeg > 0.0f ? PxD6Motion::eLIMITED : PxD6Motion::eLOCKED);
	}

	// PhysX는 0도나 180도 이상의 한계를 받지 않는다. 잠근 축도 한계 값 자체는 유효해야 한다.
	float LimitToRadians(float LimitDeg)
	{
		return std::clamp(LimitDeg, 1.0f, 179.0f) * (PxPi / 180.0f);
	}
}

void FConstraintInstance::InitConstraint(const FConstraintSetup& Setup, FBodyInstance* ChildBody, FBodyInstance* ParentBody, float Scale)
{
	if (Joint || !ChildBody || !ParentBody)
	{
		return;
	}

	PxRigidActor* ChildActor  = ChildBody->GetPxRigidActor();
	PxRigidActor* ParentActor = ParentBody->GetPxRigidActor();
	if (!ChildActor || !ParentActor)
	{
		return;
	}

	// UE와 같이 부모가 actor0, 자식이 actor1. Twist·Swing은 부모 프레임 기준으로 잰 자식 프레임의 회전이다.
	// 바인드 포즈에서 두 프레임이 겹치도록 설계도를 만들었으므로, 처음엔 모든 각도가 0에서 시작한다.
	Joint = PxD6JointCreate(*FPhysicsCore::Get().GetPhysics(),
		ParentActor, ToJointFrame(Setup.ParentFrame, Scale),
		ChildActor,  ToJointFrame(Setup.ChildFrame, Scale));
	if (!Joint)
	{
		return;
	}

	// 위치(X·Y·Z)는 기본값대로 잠겨 있다. 회전만 한계 안에서 움직인다.
	SetAngularMotion(*Joint, PxD6Axis::eTWIST,  Setup.TwistLimitDeg);
	SetAngularMotion(*Joint, PxD6Axis::eSWING1, Setup.Swing1LimitDeg);
	SetAngularMotion(*Joint, PxD6Axis::eSWING2, Setup.Swing2LimitDeg);

	const float Twist = LimitToRadians(Setup.TwistLimitDeg);
	Joint->setTwistLimit(PxJointAngularLimitPair(-Twist, Twist));
	Joint->setSwingLimit(PxJointLimitCone(LimitToRadians(Setup.Swing1LimitDeg), LimitToRadians(Setup.Swing2LimitDeg)));

	// Collision 쇼 플래그에서 관절 프레임과 한계를 그린다.
	Joint->setConstraintFlag(PxConstraintFlag::eVISUALIZATION, true);
}

void FConstraintInstance::TermConstraint()
{
	if (Joint)
	{
		Joint->release();
		Joint = nullptr;
	}
}
