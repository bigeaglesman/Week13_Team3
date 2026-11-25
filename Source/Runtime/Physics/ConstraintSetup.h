#pragma once

#include "Core/Types.h"
#include "Math/Transform.h"
#include "ConstraintSetup.generated.h"

// 관절 한 축의 자유도. UE의 ELinearConstraintMotion / EAngularConstraintMotion을 하나로 합친 것 (PhysX의 PxD6Motion)
//   Free    = 마음대로 움직인다 (자유도 1)
//   Limited = 한계 안에서만 움직인다
//   Locked  = 못 움직인다 (자유도 0)
// 6축(이동 X·Y·Z + 회전 Twist·Swing1·Swing2)을 각각 정하면 모든 조인트를 D6 하나로 표현할 수 있다.
//   Fixed     : 6축 Locked
//   Hinge     : Twist만 Free/Limited (문 경첩, PxRevoluteJoint)
//   Prismatic : 이동 X만 Free/Limited (슬라이더, PxPrismaticJoint)
//   Ball      : 회전 3축 Free/Limited (PxSphericalJoint, 래그돌 관절)
UENUM()
enum class EConstraintMotion : uint8
{
	Free,
	Limited,
	Locked,
};

// 두 바디를 잇는 관절 하나의 설계도. UE 4.27의 FConstraintInstance 설정부(FConstraintProfileProperties) 자리다.
// 런타임에는 이걸로 PxD6Joint를 만든다. Physics Asset의 래그돌 관절과 UPhysicsConstraintComponent가 함께 쓴다.
//
// 관절 프레임: 두 바디 각각의 로컬 공간에 놓인 좌표계. 시작 포즈에서 두 프레임은 월드에서 같은 자리·같은 방향이다.
//   X축 = Twist(비틀림) 축. 뼈 길이 방향으로 둔다. 이동 X도 이 축이다.
//   Y축 = Swing1 축, Z축 = Swing2 축. 뼈가 옆으로 꺾이는 방향이다.
// PhysX D6 관절과 축 규칙이 같다.
struct FConstraintSetup
{
	FString JointName;
	FString ChildBoneName;    // UE의 ConstraintBone1
	FString ParentBoneName;   // UE의 ConstraintBone2

	// 위치는 메시 원본 단위. 스케일은 쓰지 않는다. 부모 바디가 없으면(월드에 고정) ParentFrame은 월드 좌표다.
	FTransform ChildFrame;
	FTransform ParentFrame;

	// ---- 이동 자유도. 관절은 보통 3축 모두 잠근다 ----
	EConstraintMotion LinearXMotion = EConstraintMotion::Locked;
	EConstraintMotion LinearYMotion = EConstraintMotion::Locked;
	EConstraintMotion LinearZMotion = EConstraintMotion::Locked;
	// Limited인 이동 축이 프레임 원점에서 벗어날 수 있는 거리 (m, 메시 원본 단위). 세 축이 같은 값을 쓴다 (PhysX 제약)
	float LinearLimit = 0.0f;

	// ---- 회전 자유도 ----
	// Limited이면서 한계가 0이면 Locked로 다룬다 (자동 생성한 래그돌에서 축을 잠그는 방법).
	EConstraintMotion TwistMotion  = EConstraintMotion::Limited;
	EConstraintMotion Swing1Motion = EConstraintMotion::Limited;
	EConstraintMotion Swing2Motion = EConstraintMotion::Limited;

	// 프레임 X축 기준 비틀림 한계 (±도)
	float TwistLimitDeg = 45.0f;
	// 원뿔 한계. Y축(Swing1), Z축(Swing2) 방향으로 몇 도까지 꺾이는지
	float Swing1LimitDeg = 45.0f;
	float Swing2LimitDeg = 45.0f;

	// D6에 적용하는 실제 회전 반각. 자유축은 180도, 잠긴 축은 0도다.
	static float AngularExtentDeg(EConstraintMotion Motion, float LimitDeg)
	{
		if (Motion == EConstraintMotion::Free) return 180.0f;
		if (Motion == EConstraintMotion::Locked || LimitDeg <= 0.0f) return 0.0f;
		return FMath::Clamp(LimitDeg, 1.0f, 179.0f);
	}

	// ---- 그 밖 ----
	// 이어진 두 바디끼리 부딪히지 않게 한다. (UE 기본값과 같음)
	bool bDisableCollision = true;

	// 회전을 시작 포즈 쪽으로 되돌리는 스프링·감쇠 (SLERP 드라이브). 둘 다 0이면 드라이브를 쓰지 않는다.
	// 감쇠만 주면 흔들림이 빨리 잦아든다 (진자, 래그돌 안정화).
	float AngularDriveStiffness = 0.0f;
	float AngularDriveDamping = 0.0f;
};
