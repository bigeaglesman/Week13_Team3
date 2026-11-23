#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXPublic.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <vector>

using namespace physx;

namespace
{
	bool BuildPxGeometry(const FBodyShapeDesc& ShapeDesc, PxGeometryHolder& OutGeometry, PxTransform& OutLocalPose)
	{
		// PhysX는 0 이하의 반크기·반지름·높이를 가진 Geometry로 Shape를 만들면 실패한다.
		constexpr float MinExtent = 1e-4f;
		const PxVec3 LocalOffset = U2PVector(ShapeDesc.LocalOffset);
		const PxQuat LocalRot = U2PQuat(ShapeDesc.LocalRotation);

		switch (ShapeDesc.Type)
		{
		case EBodyShapeType::Box:
		{
			const PxVec3 HalfExtents(
				std::max(ShapeDesc.HalfExtent.X, MinExtent),
				std::max(ShapeDesc.HalfExtent.Y, MinExtent),
				std::max(ShapeDesc.HalfExtent.Z, MinExtent));
			OutGeometry.storeAny(PxBoxGeometry(HalfExtents));
			OutLocalPose = PxTransform(LocalOffset, LocalRot);
			return true;
		}
		case EBodyShapeType::Sphere:
		{
			const float Radius = std::max(ShapeDesc.Radius, MinExtent);
			OutGeometry.storeAny(PxSphereGeometry(Radius));
			OutLocalPose = PxTransform(LocalOffset, LocalRot);
			return true;
		}
		case EBodyShapeType::Capsule:
		{
			const float Radius = std::max(ShapeDesc.Radius, MinExtent);
			// UCapsuleComponent::HalfHeight는 양쪽 반구를 포함한 전체 반높이이지만,
			// PxCapsuleGeometry::halfHeight는 가운데 원기둥 부분만의 반높이다.
			const float CylinderHalfHeight = std::max(ShapeDesc.HalfHeight - Radius, MinExtent);
			OutGeometry.storeAny(PxCapsuleGeometry(Radius, CylinderHalfHeight));

			// PhysX 캡슐은 로컬 X축이 축이므로 먼저 Y축 90도 회전해 UE Z축으로 맞춘 뒤,
			// 그 위에 ShapeDesc.LocalRotation을 곱해 원하는 본 축 방향으로 정렬한다.
			OutLocalPose = PxTransform(LocalOffset, LocalRot * PxQuat(PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f)));
			return true;
		}
		default:
			return false;
		}
	}

		// 씬에 등록된 Dynamic(비-Kinematic) 액터만 반환한다.
		PxRigidDynamic* GetSimulatingDynamicActor(PxRigidActor * RigidActor)
		{
			if (!RigidActor || !RigidActor->getScene())
			{
				return nullptr;
			}

			PxRigidDynamic* DynamicActor = RigidActor->is<PxRigidDynamic>();
			if (!DynamicActor || (DynamicActor->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
			{
				return nullptr;
			}

			return DynamicActor;
		}

	void ApplyShapeFilterAndFlags(PxShape* Shape, const FBodyInstance& Body)
	{
		if (!Shape)
		{
			return;
		}

		// Dynamic 강체는 시뮬레이션 도형이 필요하므로 Trigger 플래그를 허용하지 않는다.
		const bool bUseTrigger = Body.bIsTrigger && !Body.bSimulatePhysics;

		// PhysX 규칙: eSIMULATION_SHAPE과 eTRIGGER_SHAPE은 상호 배타적이다.
		// 반드시 기존 플래그를 먼저 끈 뒤 새 플래그를 켜야 에러가 나지 않는다.
		if (bUseTrigger)
		{
			Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
			Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
		}
		else
		{
			Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
			Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
		}

		PxFilterData FilterData;
		FilterData.word0 = CollisionChannelToBit(Body.ObjectType);
		FilterData.word1 = Body.BlockMask;
		FilterData.word2 = Body.OverlapMask;
		FilterData.word3 = Body.ActorUniqueID;

		Shape->setSimulationFilterData(FilterData);
		Shape->setQueryFilterData(FilterData);
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

	if (bSimulatePhysics || bKinematic)
	{
		DynamicActor = GPhysXSDK->createRigidDynamic(InitialPose);
		if (!DynamicActor)
		{
			HTR_LOG(Error, "[PhysX] createRigidDynamic failed");
			return;
		}
		if (!bSimulatePhysics && bKinematic)
		{
			DynamicActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
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

	// Exclusive Shape로 생성(액터 전용)하고, 로컬 포즈·필터·userData를 설정한다.
	PxShape* Shape = GPhysXSDK->createShape(Geometry.any(), *GPhysXDefaultMaterial, true);
	if (!Shape)
	{
		HTR_LOG(Error, "[PhysX] createShape failed");
		NewActor->release();
		return;
	}

	Shape->setLocalPose(LocalPose);
	Shape->userData = InOwnerComp;
	ApplyShapeFilterAndFlags(Shape, *this);

	// attachShape가 참조 카운트를 올리므로, 여기서 로컬 참조를 release해야 Actor 해제 시 Shape도 함께 해제된다.
	NewActor->attachShape(*Shape);
	Shape->release();

	if (DynamicActor && bSimulatePhysics)
	{
		// Shape가 붙은 뒤에 호출해야 도형 크기와 LocalPose를 반영해 정확한 관성 텐서와 무게중심이 계산된다.
		PxRigidBodyExt::setMassAndUpdateInertia(*DynamicActor, std::max(MassInKg, 0.001f));
		DynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bEnableGravity);
		DynamicActor->setLinearDamping(std::max(LinearDamping, 0.0f));
		DynamicActor->setAngularDamping(std::max(AngularDamping, 0.0f));
		DynamicActor->setSolverIterationCounts(
			std::max(PositionSolverIterationCount, 1u),
			std::max(VelocitySolverIterationCount, 1u));
		DynamicActor->setMaxDepenetrationVelocity(2.0f);
		DynamicActor->setSleepThreshold(0.05f);
		DynamicActor->setStabilizationThreshold(0.05f);
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

void FBodyInstance::UpdatePhysicsFilterData()
{
	if (!RigidActor)
	{
		return;
	}

	const PxU32 NumShapes = RigidActor->getNbShapes();
	if (NumShapes == 0)
	{
		return;
	}

	std::vector<PxShape*> Shapes(NumShapes);
	RigidActor->getShapes(Shapes.data(), NumShapes);

	for (PxShape* Shape : Shapes)
	{
		ApplyShapeFilterAndFlags(Shape, *this);
	}
}
void FBodyInstance::AddForce(const FVector& Force, bool bAccelChange)
{
	PxRigidDynamic* DynamicActor = GetSimulatingDynamicActor(RigidActor);
	if (!DynamicActor)
	{
		return;
	}

	const PxForceMode::Enum Mode = bAccelChange ? PxForceMode::eACCELERATION : PxForceMode::eFORCE;
	DynamicActor->addForce(U2PVector(Force), Mode, true);
}

void FBodyInstance::AddImpulse(const FVector& Impulse, bool bVelChange)
{
	PxRigidDynamic* DynamicActor = GetSimulatingDynamicActor(RigidActor);
	if (!DynamicActor)
	{
		return;
	}

	const PxForceMode::Enum Mode = bVelChange ? PxForceMode::eVELOCITY_CHANGE : PxForceMode::eIMPULSE;
	DynamicActor->addForce(U2PVector(Impulse), Mode, true);
}

void FBodyInstance::AddImpulseAtPosition(const FVector& Impulse, const FVector& Position)
{
	PxRigidDynamic* DynamicActor = GetSimulatingDynamicActor(RigidActor);
	if (!DynamicActor)
	{
		return;
	}

	PxRigidBodyExt::addForceAtPos(
		*DynamicActor,
		U2PVector(Impulse),
		U2PVector(Position),
		PxForceMode::eIMPULSE,
		true);
}