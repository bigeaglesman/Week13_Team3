#pragma once

#include "Components/SceneComponent.h"
#include "Physics/ConstraintInstance.h"
#include "PhysicsConstraintComponent.generated.h"

class UPrimitiveComponent;

// 두 UPrimitiveComponent(또는 월드 고정점)를 FConstraintInstance로 연결하는 컴포넌트
// (언리얼 UPhysicsConstraintComponent 대응)
UCLASS()
class UPhysicsConstraintComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPhysicsConstraintComponent() = default;
	virtual ~UPhysicsConstraintComponent() override = default;

	virtual void BeginPlay() override;

	// 두 컴포넌트를 현재 PhysicsConstraintComponent의 월드 위치·회전을 피벗으로 연결한다.
	// Comp1 또는 Comp2가 nullptr이면 월드 공간 고정점에 매단다.
	void SetConstrainedComponents(UPrimitiveComponent* Comp1, UPrimitiveComponent* Comp2);

	// 현재 연결된 조인트를 해제한다.
	void BreakConstraint();

	FConstraintInstance ConstraintInstance;

	// 씬 JSON/에디터에서 대상 액터 이름으로 연결할 때 사용
	// Actor1Name이 비어 있으면 월드 고정점, Actor2Name이 비어 있으면 자신의 Owner 액터를 사용한다.
	UPROPERTY(EditAnywhere)
	FString Actor1Name;
	UPROPERTY(EditAnywhere)
	FString Actor2Name;

	UPROPERTY(EditAnywhere)
	bool bDisableCollision = true;
	UPROPERTY(EditAnywhere)
	float Swing1LimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere)
	float Swing2LimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere)
	float TwistLimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere)
	float AngularDamping = 0.5f;
	UPROPERTY(EditAnywhere)
	float AngularStiffness = 0.0f;

protected:
	virtual bool ShouldCreatePhysicsState() const override { return true; }
	virtual void OnDestroyPhysicsState() override;
};