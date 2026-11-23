#pragma once

#include "Components/SceneComponent.h"
#include "Physics/ConstraintSetup.h"
#include "Physics/ConstraintInstance.h"
#include "PhysicsConstraintComponent.generated.h"

class UPrimitiveComponent;
struct FBodyInstance;

// 두 액터의 물리 바디(또는 액터 하나와 월드)를 관절로 잇는 컴포넌트. UE의 UPhysicsConstraintComponent 자리다.
// 이 컴포넌트의 월드 위치·회전이 관절 피벗이다. 피벗의 X축이 Twist(비틀림)·이동 X 축이다.
//
// 6자유도(이동 X·Y·Z + 회전 Twist·Swing1·Swing2)를 축마다 Free / Limited / Locked로 정한다. (PxD6Joint)
//   Fixed     : 전부 Locked                   (PxFixedJoint)
//   Hinge     : Twist만 Free 또는 Limited       (PxRevoluteJoint, 문 경첩)
//   Prismatic : 이동 X만 Free 또는 Limited       (PxPrismaticJoint, 슬라이더)
//   Ball      : 회전 3축 Free 또는 Limited       (PxSphericalJoint, 진자·체인)
//
// 연결은 첫 Tick에 한다. BeginPlay 순서에 따라 상대 액터의 바디가 아직 없을 수 있어서다.
UCLASS()
class UPhysicsConstraintComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPhysicsConstraintComponent();
	virtual ~UPhysicsConstraintComponent() override = default;

	virtual void TickComponent(float DeltaTime) override;

	// ---- 연결 대상 (UE의 ConstraintActor1 / ConstraintActor2) ----
	// 부모 쪽 액터 이름. 비우면 월드 고정점에 매단다.
	UPROPERTY(EditAnywhere)
	FString Actor1Name;
	// 자식 쪽 액터 이름. 비우면 이 컴포넌트를 가진 액터.
	UPROPERTY(EditAnywhere)
	FString Actor2Name;

	// ---- 이동 자유도 ----
	UPROPERTY(EditAnywhere)
	EConstraintMotion LinearXMotion = EConstraintMotion::Locked;
	UPROPERTY(EditAnywhere)
	EConstraintMotion LinearYMotion = EConstraintMotion::Locked;
	UPROPERTY(EditAnywhere)
	EConstraintMotion LinearZMotion = EConstraintMotion::Locked;
	// Limited인 이동 축의 한계 거리 (m)
	UPROPERTY(EditAnywhere)
	float LinearLimit = 0.0f;

	// ---- 회전 자유도 ----
	UPROPERTY(EditAnywhere)
	EConstraintMotion TwistMotion = EConstraintMotion::Limited;
	UPROPERTY(EditAnywhere)
	EConstraintMotion Swing1Motion = EConstraintMotion::Limited;
	UPROPERTY(EditAnywhere)
	EConstraintMotion Swing2Motion = EConstraintMotion::Limited;
	UPROPERTY(EditAnywhere)
	float TwistLimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere)
	float Swing1LimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere)
	float Swing2LimitDegrees = 45.0f;

	// ---- 그 밖 ----
	// 이어진 두 바디끼리 부딪히지 않게 한다.
	UPROPERTY(EditAnywhere)
	bool bDisableCollision = true;
	// 시작 포즈로 되돌리는 회전 스프링과 감쇠. 감쇠만 주면 흔들림이 빨리 잦아든다.
	UPROPERTY(EditAnywhere)
	float AngularDriveStiffness = 0.0f;
	UPROPERTY(EditAnywhere)
	float AngularDriveDamping = 0.0f;

	// 두 컴포넌트를 지금 이 컴포넌트의 위치·회전을 피벗으로 잇는다. 한쪽이 nullptr이면 그쪽은 월드에 고정된다.
	// 이미 이어져 있으면 끊고 다시 잇는다. 성공하면 true
	bool SetConstrainedComponents(UPrimitiveComponent* Component1, UPrimitiveComponent* Component2);

	// 관절을 끊는다 (UE의 BreakConstraint).
	void BreakConstraint();

	bool IsConstraintActive() const { return ConstraintInstance.IsValidConstraintInstance(); }

	// 피벗 (위치 + 회전, 스케일 없음)
	FTransform GetPivotWorldTransform() const;

	// 위 UPROPERTY들로 만든 설계도. 프레임은 비어 있다 (SetConstrainedComponents가 채운다).
	FConstraintSetup MakeConstraintSetup() const;

	// 이름으로 찾은 연결 대상의 물리 바디를 가진 컴포넌트. Collision 쇼 플래그도 이걸로 연결선을 그린다.
	UPrimitiveComponent* FindConstrainedComponent(const FString& ActorName, bool bUseOwnerIfEmpty) const;

protected:
	virtual bool ShouldCreatePhysicsState() const override { return true; }
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;

private:
	FConstraintInstance ConstraintInstance;
	bool bConnectAttempted = false;
};
