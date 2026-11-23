#pragma once

#include "Core/Types.h"
#include "Math/Transform.h"

struct FBodyInstance;

namespace physx
{
	class PxD6Joint;
}

// 선형 자유도 제한 모드 (언리얼 ELinearConstraintMotion 대응)
enum class ELinearConstraintMotion : uint8
{
	LCM_Free = 0,
	LCM_Limited,
	LCM_Locked
};

// 회전 자유도 제한 모드 (언리얼 EAngularConstraintMotion 대응)
enum class EAngularConstraintMotion : uint8
{
	ACM_Free = 0,
	ACM_Limited,
	ACM_Locked
};

// 두 FBodyInstance를 PhysX PxD6Joint로 연결하는 조인트 인스턴스 (언리얼 FConstraintInstance 대응)
struct FConstraintInstance
{
public:
	FConstraintInstance() = default;
	~FConstraintInstance();

	FConstraintInstance(const FConstraintInstance&) = delete;
	FConstraintInstance& operator=(const FConstraintInstance&) = delete;

	// 두 바디를 조인트로 연결한다. Body1 또는 Body2 중 하나가 nullptr이면 월드 공간 고정점에 매단다.
	bool InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2);

	// PhysX PxD6Joint를 해제한다.
	void TermConstraint();

	bool IsValidConstraintInstance() const { return ConstraintData != nullptr; }
	physx::PxD6Joint* GetPxJoint() const { return ConstraintData; }

	// 현재 설정을 이미 생성된 PxD6Joint에 반영한다.
	void UpdateLinearLimit();
	void UpdateAngularLimit();
	void UpdateAngularDrive();

	// 각 바디 로컬 공간에서의 조인트 피벗 Transform 설정 (FrameIndex: 0 = Body1, 1 = Body2)
	void SetRefFrame(int32 FrameIndex, const FTransform& InRefFrame);

public:
	// 각 바디 로컬 공간 기준 조인트 위치·방향 (상대 Body가 nullptr이면 월드 공간 기준)
	FTransform RefFrame1 = FTransform::Identity;
	FTransform RefFrame2 = FTransform::Identity;

	// 선형 3축 자유도 (관절/진자는 기본 모두 Locked)
	ELinearConstraintMotion XMotion = ELinearConstraintMotion::LCM_Locked;
	ELinearConstraintMotion YMotion = ELinearConstraintMotion::LCM_Locked;
	ELinearConstraintMotion ZMotion = ELinearConstraintMotion::LCM_Locked;
	float LinearLimitSize = 0.0f;

	// 회전 3축 자유도 (X=Twist, Y=Swing1, Z=Swing2 — PhysX D6 규약)
	EAngularConstraintMotion Swing1Motion = EAngularConstraintMotion::ACM_Limited;
	EAngularConstraintMotion Swing2Motion = EAngularConstraintMotion::ACM_Limited;
	EAngularConstraintMotion TwistMotion = EAngularConstraintMotion::ACM_Limited;

	// 각 축 허용 각도 (도 단위, 0 ~ 180)
	float Swing1LimitDegrees = 45.0f;
	float Swing2LimitDegrees = 45.0f;
	float TwistLimitDegrees = 45.0f;

	// 관절 회전 감쇠·스프링 (SLERP 드라이브, 가속도 모드 — 래그돌 안정화용)
	float AngularStiffness = 0.0f;
	float AngularDamping = 0.5f;

	// 연결된 두 바디 간 자기 충돌 비활성화 여부
	bool bDisableCollision = true;

private:
	physx::PxD6Joint* ConstraintData = nullptr;
};