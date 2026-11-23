#pragma once

struct FConstraintSetup;
struct FBodyInstance;

namespace physx
{
	class PxD6Joint;
}

// 두 바디를 잇는 런타임 관절 하나 (PxD6Joint). UE 4.27의 FConstraintInstance 자리다.
// FConstraintSetup(설계도)으로 만들고, 스켈레탈 메시 컴포넌트가 들고 있다가 지운다.
class FConstraintInstance
{
public:
	FConstraintInstance() = default;
	~FConstraintInstance() { TermConstraint(); }

	FConstraintInstance(const FConstraintInstance&) = delete;
	FConstraintInstance& operator=(const FConstraintInstance&) = delete;

	// Scale: 컴포넌트 월드 스케일. 설계도의 프레임 위치는 메시 원본 단위라 곱해서 쓴다.
	void InitConstraint(const FConstraintSetup& Setup, FBodyInstance* ChildBody, FBodyInstance* ParentBody, float Scale);
	void TermConstraint();

	bool IsValidConstraintInstance() const { return Joint != nullptr; }

private:
	physx::PxD6Joint* Joint = nullptr;
};
