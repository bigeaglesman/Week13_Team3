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

	const PxTransform InitialPose = U2PTransform(WorldTransform);
	PxRigidActor* NewActor = nullptr;
	PxRigidDynamic* DynamicActor = nullptr;

	if (bSimulatePhysics)
	{
		DynamicActor = GPhysXSDK->createRigidDynamic(InitialPose);
		if (!DynamicActor)
		{
			HTR_LOG(Error, "[PhysX] createRigidDynamic failed");
			return;
		}
		NewActor = DynamicActor;
	}
	else
	{
		PxRigidStatic* StaticActor = GPhysXSDK->createRigidStatic(InitialPose);
		if (!StaticActor)
		{
			HTR_LOG(Error, "[PhysX] createRigidStatic failed");
			return;
		}
		NewActor = StaticActor;
	}

	// Exclusive Shape로 생성(액터 전용)하고, 로컬 포즈와 userData를 설정한다.
	PxShape* Shape = GPhysXSDK->createShape(Geometry.any(), *GPhysXDefaultMaterial, true);
	if (!Shape)
	{
		HTR_LOG(Error, "[PhysX] createShape failed");
		NewActor->release();
		return;
	}

	Shape->setLocalPose(LocalPose);
	Shape->userData = InOwnerComp;

	// attachShape가 참조 카운트를 올리므로, 여기서 로컬 참조를 release해야 Actor 해제 시 Shape도 함께 해제된다.
	NewActor->attachShape(*Shape);
	Shape->release();

	if (DynamicActor)
	{
		// Shape가 붙은 뒤에 호출해야 도형 크기와 LocalPose를 반영해 정확한 관성 텐서와 무게중심이 계산된다.
		PxRigidBodyExt::setMassAndUpdateInertia(*DynamicActor, std::max(MassInKg, 0.001f));
		DynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bEnableGravity);
		DynamicActor->setLinearDamping(std::max(LinearDamping, 0.0f));
		DynamicActor->setAngularDamping(std::max(AngularDamping, 0.0f));
	}

	NewActor->userData = InOwnerComp;

	InPhysScene->GetPxScene()->addActor(*NewActor);

	RigidActor = NewActor;
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

FTransform FBodyInstance::GetUnrealWorldTransform() const
{
	if (!RigidActor)
	{
		return FTransform::Identity;
	}

	return P2UTransform(RigidActor->getGlobalPose());
}

FVector FBodyInstance::GetUnrealWorldVelocity() const
{
	if (!RigidActor)
	{
		return FVector::ZeroVector;
	}

	if (const PxRigidDynamic* DynamicActor = RigidActor->is<PxRigidDynamic>())
	{
		return P2UVector(DynamicActor->getLinearVelocity());
	}

	return FVector::ZeroVector;
}

bool FBodyInstance::IsInstanceSimulatingPhysics() const
{
	if (!RigidActor)
	{
		return false;
	}

	const PxRigidDynamic* DynamicActor = RigidActor->is<PxRigidDynamic>();
	if (!DynamicActor)
	{
		return false;
	}

	return !(DynamicActor->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC);
}

void FBodyInstance::SetEnableGravity(bool bGravityEnabled)
{
	bEnableGravity = bGravityEnabled;

	if (PxRigidDynamic* DynamicActor = RigidActor ? RigidActor->is<PxRigidDynamic>() : nullptr)
	{
		DynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bEnableGravity);
		if (bEnableGravity)
		{
			DynamicActor->wakeUp();
		}
	}
}