#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXPublic.h"

#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

namespace
{
	bool BuildPxGeometry(const FBodyShapeDesc& ShapeDesc, PxGeometryHolder& OutGeometry, PxTransform& OutLocalPose)
	{
		// PhysX는 0 이하의 반크기·반지름·높이를 가진 Geometry로 Shape를 만들면 실패한다.
		constexpr float MinExtent = 1e-4f;
		const PxVec3 LocalOffset = U2PVector(ShapeDesc.LocalOffset);

		switch (ShapeDesc.Type)
		{
		case EBodyShapeType::Box:
		{
			const PxVec3 HalfExtents(
				std::max(ShapeDesc.HalfExtent.X, MinExtent),
				std::max(ShapeDesc.HalfExtent.Y, MinExtent),
				std::max(ShapeDesc.HalfExtent.Z, MinExtent));
			OutGeometry.storeAny(PxBoxGeometry(HalfExtents));
			OutLocalPose = PxTransform(LocalOffset);
			return true;
		}
		case EBodyShapeType::Sphere:
		{
			const float Radius = std::max(ShapeDesc.Radius, MinExtent);
			OutGeometry.storeAny(PxSphereGeometry(Radius));
			OutLocalPose = PxTransform(LocalOffset);
			return true;
		}
		case EBodyShapeType::Capsule:
		{
			const float Radius = std::max(ShapeDesc.Radius, MinExtent);
			// UCapsuleComponent::HalfHeight는 양쪽 반구를 포함한 전체 반높이이지만,
			// PxCapsuleGeometry::halfHeight는 가운데 원기둥 부분만의 반높이다.
			const float CylinderHalfHeight = std::max(ShapeDesc.HalfHeight - Radius, MinExtent);
			OutGeometry.storeAny(PxCapsuleGeometry(Radius, CylinderHalfHeight));

			// PhysX 캡슐은 로컬 X축이 캡슐 축이고, 엔진(UE) 캡슐은 로컬 Z축이 캡슐 축이다.
			// Y축 기준 90도 회전해 X축을 Z축 방향으로 정렬한다.
			OutLocalPose = PxTransform(LocalOffset, PxQuat(PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f)));
			return true;
		}
		default:
			return false;
		}
	}
}

FBodyInstance::~FBodyInstance()
{
	TermBody();
}

void FBodyInstance::InitBody(
	const FBodyShapeDesc& ShapeDesc,
	const FTransform& WorldTransform,
	UPrimitiveComponent* InOwnerComp,
	FPhysScene* InPhysScene)
{
	// 이미 생성된 바디가 있으면 먼저 정리한다.
	if (RigidActor)
	{
		TermBody();
	}

	if (!GPhysXSDK || !GPhysXDefaultMaterial || !InPhysScene || !InPhysScene->GetPxScene())
	{
		return;
	}

	PxGeometryHolder Geometry;
	PxTransform LocalPose(PxIdentity);
	if (!BuildPxGeometry(ShapeDesc, Geometry, LocalPose))
	{
		return;
	}

	// 4단계에서는 모든 충돌체를 Static 액터로 생성한다.
	PxRigidStatic* StaticActor = GPhysXSDK->createRigidStatic(U2PTransform(WorldTransform));
	if (!StaticActor)
	{
		HTR_LOG(Error, "[PhysX] createRigidStatic failed");
		return;
	}

	// Exclusive Shape로 생성(액터 전용)하고, 로컬 포즈와 userData를 설정한다.
	PxShape* Shape = GPhysXSDK->createShape(Geometry.any(), *GPhysXDefaultMaterial, true);
	if (!Shape)
	{
		HTR_LOG(Error, "[PhysX] createShape failed");
		StaticActor->release();
		return;
	}

	Shape->setLocalPose(LocalPose);
	Shape->userData = InOwnerComp;

	// attachShape가 참조 카운트를 올리므로, 여기서 로컬 참조를 release해야 Actor 해제 시 Shape도 함께 해제된다.
	StaticActor->attachShape(*Shape);
	Shape->release();

	StaticActor->userData = InOwnerComp;

	InPhysScene->GetPxScene()->addActor(*StaticActor);

	RigidActor = StaticActor;
	OwnerComponent = InOwnerComp;
}

void FBodyInstance::TermBody()
{
	if (RigidActor)
	{
		// getScene()으로 현재 소속된 씬을 확인한다.
		// UWorld 소멸 과정에서 PxScene이 먼저 해제되었더라도 getScene()은 nullptr을 반환하므로 안전하다.
		if (PxScene* Scene = RigidActor->getScene())
		{
			Scene->removeActor(*RigidActor);
		}
		RigidActor->release();
		RigidActor = nullptr;
	}

	OwnerComponent = nullptr;
}

void FBodyInstance::SetBodyTransform(const FTransform& NewTransform)
{
	if (!RigidActor)
	{
		return;
	}

	RigidActor->setGlobalPose(U2PTransform(NewTransform));
}