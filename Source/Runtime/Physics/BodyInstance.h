#pragma once

#include "Core/Types.h"
#include "Math/Transform.h"

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

	bool IsValidBodyInstance() const { return RigidActor != nullptr; }
	physx::PxRigidActor* GetPxRigidActor() const { return RigidActor; }
	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }

private:
	// 4단계에서는 PxRigidStatic을 담고, 5단계에서 PxRigidDynamic도 같은 공통 부모 포인터로 다룬다.
	physx::PxRigidActor* RigidActor = nullptr;
	UPrimitiveComponent* OwnerComponent = nullptr;
};