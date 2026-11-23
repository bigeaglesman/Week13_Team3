#pragma once

#include "Math/Transform.h"
#include "BodyInstance.generated.h"

class UPrimitiveComponent;
class UPhysicalMaterial;
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

	// 다른 바디와 처음 닿을 때 OnComponentHit을 보낼지. UE의 bNotifyRigidBodyCollision (Simulation Generates Hit Events)
	UPROPERTY(EditAnywhere)
	bool bNotifyRigidBodyCollision = false;

	// 이 바디의 모든 도형에 쓸 표면 재질. nullptr이면 엔진 기본 재질. InitBody 전에 정한다.
	// 에셋(UBodySetup 등)이 소유하고, 바디는 빌려 쓴다. UE의 PhysMaterialOverride
	UPhysicalMaterial* PhysMaterialOverride = nullptr;

	// WorldTransform의 스케일은 쓰지 않는다. 스케일은 Geom 크기에 이미 들어 있다.
	void InitBody(UPrimitiveComponent* InOwner, const FKAggregateGeom& Geom, const FTransform& WorldTransform, FPhysicsScene* Scene);
	void TermBody();

	bool IsValidBodyInstance() const { return RigidActor != nullptr; }
	bool IsSimulating() const { return RigidActor && bSimulatePhysics; }

	// 컴포넌트 → 바디. 키네마틱 바디만 따라간다. 시뮬레이션 중인 바디는 PhysX가 주인이다.
	void SetBodyTransform(const FTransform& WorldTransform);

	// 지금 바디의 월드 위치·회전 (스케일 없음)
	FTransform GetUnrealWorldTransform() const;

	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }
	physx::PxRigidActor* GetPxRigidActor() const { return RigidActor; }

	// 스켈레탈 메시의 뼈 바디면 뼈 인덱스, 아니면 -1. UE의 InstanceBoneIndex
	int32 InstanceBoneIndex = -1;

	// 0이 아니고 같은 값을 가진 바디끼리는 서로 충돌하지 않는다. InitBody 전에 정한다.
	// 래그돌 뼈 캡슐들은 서로 겹쳐 있어서, 부딪히게 두면 시작하자마자 튕겨 나간다. 스켈레탈 메시가 자기 UUID를 넣는다.
	uint32 SelfCollisionGroup = 0;

	// 필터 셰이더가 읽는 PxFilterData.word1 비트
	static constexpr uint32 FilterFlag_NotifyHit = 1u << 0;

private:
	UPrimitiveComponent* OwnerComponent = nullptr;
	physx::PxRigidActor* RigidActor = nullptr;
};

// 행렬(뼈 월드 행렬 등)에서 바디 포즈용 위치 + 회전만 뽑는다. 스케일은 버린다.
FTransform MatrixToBodyTransform(const FMatrix& Matrix);
