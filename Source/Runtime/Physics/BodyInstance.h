#pragma once

#include "Math/Transform.h"
#include "BodyInstance.generated.h"

class UPrimitiveComponent;
class FPhysicsScene;
struct FKAggregateGeom;

namespace physx
{
	class PxRigidActor;
}

// 컴포넌트 하나에 붙는 PhysX 강체 하나. UE 4.27의 FBodyInstance 자리다.
// 설정(UPROPERTY)은 에디터에서 고치고, PxRigidActor는 BeginPlay에서 만들어 OnUnregister에서 지운다.
//   bSimulatePhysics = true  → PxRigidDynamic. PhysX가 움직이고, 결과를 컴포넌트에 되돌려 쓴다.
//   bSimulatePhysics = false → PxRigidDynamic(키네마틱). 컴포넌트가 움직이면 따라가며 다른 바디를 민다.
USTRUCT()
struct FBodyInstance
{
	GENERATED_BODY()

	// 물리로 움직일지. 꺼져 있어도 bEnablePhysicsCollision이면 다른 바디와 부딪힌다.
	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = false;

	// 시뮬레이션은 안 해도 물리 바디는 만들지 (바닥, 벽). UE의 CollisionEnabled = PhysicsOnly 자리
	UPROPERTY(EditAnywhere)
	bool bEnablePhysicsCollision = false;

	UPROPERTY(EditAnywhere)
	bool bEnableGravity = true;

	// kg. 관성 텐서는 도형 모양에서 PhysX가 계산한다.
	UPROPERTY(EditAnywhere)
	float MassInKg = 10.0f;

	bool ShouldCreateBody() const { return bSimulatePhysics || bEnablePhysicsCollision; }

	// WorldTransform의 스케일은 쓰지 않는다. 스케일은 Geom 크기에 이미 들어 있다.
	void InitBody(UPrimitiveComponent* InOwner, const FKAggregateGeom& Geom, const FTransform& WorldTransform, FPhysicsScene* Scene);
	void TermBody();

	bool IsValidBodyInstance() const { return RigidActor != nullptr; }
	bool IsSimulating() const { return RigidActor && bSimulatePhysics; }

	// 컴포넌트 → 바디. 키네마틱 바디만 따라간다. 시뮬레이션 중인 바디는 PhysX가 주인이다.
	void SetBodyTransform(const FTransform& WorldTransform);

	// 바디 → 컴포넌트. FPhysicsScene이 움직인 액터에 대해서만 부른다.
	void SyncComponentToBody();

	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }
	physx::PxRigidActor* GetPxRigidActor() const { return RigidActor; }

private:
	UPrimitiveComponent* OwnerComponent = nullptr;
	physx::PxRigidActor* RigidActor = nullptr;
};
