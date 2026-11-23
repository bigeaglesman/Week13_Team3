#pragma once

#include "Math/Transform.h"

struct FConstraintSetup;
struct FBodyInstance;

namespace physx
{
	class PxD6Joint;
}

// 두 바디를 잇는 런타임 관절 하나 (PxD6Joint). UE 4.27의 FConstraintInstance 자리다.
// FConstraintSetup(설계도)으로 만들고, 스켈레탈 메시 컴포넌트(래그돌)나 UPhysicsConstraintComponent가 들고 있다가 지운다.
// 바디 하나를 nullptr로 주면 그쪽은 월드에 고정된다 (진자의 매단 점). 그때 그쪽 프레임은 월드 좌표다.
class FConstraintInstance
{
public:
	FConstraintInstance() = default;
	~FConstraintInstance() { TermConstraint(); }

	FConstraintInstance(const FConstraintInstance&) = delete;
	FConstraintInstance& operator=(const FConstraintInstance&) = delete;

	// Scale: 컴포넌트 월드 스케일. 설계도의 프레임 위치는 메시 원본 단위라 곱해서 쓴다.
	void InitConstraint(const FConstraintSetup& Setup, FBodyInstance* ChildBody, FBodyInstance* ParentBody, float Scale);

	// 월드 피벗 하나를 기준으로 잇는다. 두 바디의 지금 포즈에서 피벗이 각 바디 로컬로 어디인지 계산해 프레임으로 쓴다.
	// Setup의 ChildFrame·ParentFrame은 무시한다. (UPhysicsConstraintComponent용)
	void InitConstraintAtPivot(const FConstraintSetup& Setup, FBodyInstance* ChildBody, FBodyInstance* ParentBody, const FTransform& PivotWorld);

	void TermConstraint();

	bool IsValidConstraintInstance() const { return Joint != nullptr; }

private:
	physx::PxD6Joint* Joint = nullptr;
};
