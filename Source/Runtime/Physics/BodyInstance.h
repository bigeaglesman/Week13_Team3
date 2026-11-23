#pragma once

#include "Core/Types.h"
#include "Math/Transform.h"
#include "Physics/CollisionTypes.h"

class FPhysScene;
class UPrimitiveComponent;

namespace physx
{
	class PxRigidActor;
}

// 4단계에서 지원하는 기본 충돌 도형 종류
enum class EBodyShapeType : uint8
{
	None,
	Box,
	Sphere,
	Capsule
};

// 컴포넌트가 FBodyInstance에 넘겨주는 충돌 도형 정보 (월드 스케일이 이미 반영된 크기)
struct FBodyShapeDesc
{
	EBodyShapeType Type = EBodyShapeType::None;

	// Box: 각 축의 반크기 (Half Extent)
	FVector HalfExtent = FVector::ZeroVector;

	// Sphere / Capsule: 반지름
	float Radius = 0.0f;

	// Capsule: 반구 끝까지 포함한 전체 반높이 (UE UCapsuleComponent 규약)
	float HalfHeight = 0.0f;

	// 컴포넌트 원점 기준 로컬 오프셋 (스케일 반영됨, StaticMesh AABB 중심 보정용)
	FVector LocalOffset = FVector::ZeroVector;

	// 컴포넌트/본 원점 기준 로컬 회전 (래그돌 캡슐을 본 축 방향에 정렬할 때 사용)
	FQuat LocalRotation = FQuat::Identity();
};

// UPrimitiveComponent가 소유하는 물리 바디 인스턴스 (언리얼의 FBodyInstance 대응)
struct FBodyInstance
{
public:
	FBodyInstance() = default;
	~FBodyInstance();

	FBodyInstance(const FBodyInstance&) = delete;
	FBodyInstance& operator=(const FBodyInstance&) = delete;

	// PhysX Actor와 Shape를 생성해 PhysScene에 등록한다.
	void InitBody(
		const FBodyShapeDesc& ShapeDesc,
		const FTransform& WorldTransform,
		UPrimitiveComponent* InOwnerComp,
		FPhysScene* InPhysScene);

	// PhysScene에서 Actor를 제거하고 release한다.
	void TermBody();

	// 컴포넌트의 월드 Transform 변경을 PhysX Actor에 반영한다.
	void SetBodyTransform(const FTransform& NewTransform);

	// PhysX Actor의 현재 월드 Transform과 선형 속도를 엔진 좌표계로 반환한다.
	FTransform GetUnrealWorldTransform() const;
	FVector GetUnrealWorldVelocity() const;

	bool IsValidBodyInstance() const { return RigidActor != nullptr; }
	bool IsInstanceSimulatingPhysics() const;
	void SetEnableGravity(bool bGravityEnabled);

	// Dynamic 바디에 힘/충격을 가한다 (Kinematic/Static 바디에서는 무시).
	void AddForce(const FVector& Force, bool bAccelChange = false);
	void AddImpulse(const FVector& Impulse, bool bVelChange = false);
	void AddImpulseAtPosition(const FVector& Impulse, const FVector& Position);

	// 현재 필터·트리거 설정을 PhysX Shape의 FilterData와 플래그에 반영한다.
	void UpdatePhysicsFilterData();

	physx::PxRigidActor* GetPxRigidActor() const { return RigidActor; }
	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }

public:
	// InitBody 호출 전에 소유 컴포넌트가 채워 넣는 시뮬레이션 설정
	bool bSimulatePhysics = false;
	bool bKinematic = false;
	bool bEnableGravity = true;
	float MassInKg = 1.0f;
	float LinearDamping = 0.01f;
	float AngularDamping = 0.05f;
	uint32 PositionSolverIterationCount = 8;
	uint32 VelocitySolverIterationCount = 2;

	// 충돌 필터 및 트리거 설정 (PxFilterData에 인코딩된다)
	bool bIsTrigger = false;
	ECollisionChannel ObjectType = ECollisionChannel::ECC_WorldStatic;
	uint32 BlockMask = ECC_AllChannelsMask;
	uint32 OverlapMask = ECC_AllChannelsMask;
	uint32 ActorUniqueID = 0;

private:
	// PxRigidStatic / PxRigidDynamic 공통 부모 포인터
	physx::PxRigidActor* RigidActor = nullptr;
	UPrimitiveComponent* OwnerComponent = nullptr;
};