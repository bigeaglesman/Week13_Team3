#pragma once
#include <PxPhysicsAPI.h>

class UBodySetup;
class UPrimitiveComponent;
class FPhysScene;

// UE FBodyInstance. PhysX 바디 하나(PxRigidActor)의 런타임 인스턴스.
// 에셋(UBodySetup)은 여러 컴포넌트가 공유할 수 있지만, 인스턴스는 컴포넌트(래그돌은 본)마다 하나다.
struct FBodyInstance
{
	// bSimulate = true면 PhysX가 위치를 정하는 Dynamic, false면 움직이지 않는 Static 바디를 만든다.
	void InitBody(UBodySetup* Setup, const FVector& Location, const FQuat& Rotation, const FVector& Scale, bool bSimulate, UPrimitiveComponent* InOwner, FPhysScene* InScene);
	void TermBody();

	bool IsValidBodyInstance() const { return RigidActor != nullptr; }
	bool IsDynamic() const;

	FVector GetWorldLocation() const;
	FQuat GetWorldRotation() const;

	UPrimitiveComponent* OwnerComponent = nullptr;
	int32 BoneIndex = -1;   // 래그돌 본 바디면 본 인덱스, 컴포넌트 전체 바디면 -1

	physx::PxRigidActor* RigidActor = nullptr;   // userData에 이 FBodyInstance를 넣어 PhysX → 엔진으로 되찾는다
	FPhysScene* Scene = nullptr;
};
