#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/AggregateGeom.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>

using namespace physx;

namespace
{
	// PhysX 캡슐 축은 로컬 X, 엔진 캡슐 축은 로컬 Z. Y축으로 -90도 돌리면 X가 Z로 간다.
	const PxQuat CapsuleXToZ(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f));
}

FTransform MatrixToBodyTransform(const FMatrix& Matrix)
{
	return ToFTransform(ToPxNoScale(Matrix));
}

void FBodyInstance::InitBody(UPrimitiveComponent* InOwner, const FKAggregateGeom& Geom, const FTransform& WorldTransform, FPhysicsScene* Scene)
{
	if (RigidActor || !InOwner || !Scene || !Scene->IsValid() || Geom.IsEmpty())
	{
		return;
	}

	FPhysicsCore& Core = FPhysicsCore::Get();
	PxPhysics*  Physics  = Core.GetPhysics();
	PxMaterial* Material = Core.GetDefaultMaterial();

	// 시뮬레이션 여부와 상관없이 Dynamic으로 만든다. 안 움직이는 쪽은 키네마틱으로 둔다.
	// (PxRigidStatic은 옮길 때마다 브로드페이즈를 다시 짓고, 움직여도 다른 바디를 밀지 못한다.)
	PxRigidDynamic* Dynamic = Physics->createRigidDynamic(ToPx(WorldTransform));
	if (!Dynamic)
	{
		return;
	}

	// 도형마다 PxShape 하나. Exclusive = 이 액터 전용 (다른 액터와 공유하지 않음)
	for (const FKSphereElem& Elem : Geom.SphereElems)
	{
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Dynamic, PxSphereGeometry(Elem.Radius), *Material);
		Shape->setLocalPose(PxTransform(ToPx(Elem.Center)));
	}
	for (const FKBoxElem& Elem : Geom.BoxElems)
	{
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Dynamic, PxBoxGeometry(ToPx(Elem.HalfExtent)), *Material);
		Shape->setLocalPose(PxTransform(ToPx(Elem.Center), ToPx(Elem.Rotation).getNormalized()));
	}
	for (const FKSphylElem& Elem : Geom.SphylElems)
	{
		// PhysX halfHeight는 원기둥 부분의 절반
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Dynamic, PxCapsuleGeometry(Elem.Radius, Elem.Length * 0.5f), *Material);
		Shape->setLocalPose(PxTransform(ToPx(Elem.Center), ToPx(Elem.Rotation).getNormalized() * CapsuleXToZ));
	}

	if (bSimulatePhysics)
	{
		// 질량을 주면 도형 모양으로 관성 텐서와 질량 중심을 계산한다.
		PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, std::max(MassInKg, 0.001f));
		Dynamic->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bEnableGravity);
	}
	else
	{
		Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
	}

	// PxActor → FBodyInstance 역참조. 동기화 때 이걸로 컴포넌트를 찾는다.
	Dynamic->userData = this;
	Scene->GetPxScene()->addActor(*Dynamic);

	OwnerComponent = InOwner;
	RigidActor = Dynamic;
}

void FBodyInstance::TermBody()
{
	if (RigidActor)
	{
		// release가 씬에서도 빼 준다.
		RigidActor->userData = nullptr;
		RigidActor->release();
		RigidActor = nullptr;
	}
	OwnerComponent = nullptr;
}

void FBodyInstance::SetBodyTransform(const FTransform& WorldTransform)
{
	if (!RigidActor || bSimulatePhysics)
	{
		return;
	}

	// 키네마틱은 다음 simulate에서 목표 위치로 옮겨지고, 그 사이 경로에 있는 바디를 민다.
	if (PxRigidDynamic* Dynamic = RigidActor->is<PxRigidDynamic>())
	{
		Dynamic->setKinematicTarget(ToPx(WorldTransform));
	}
}

FTransform FBodyInstance::GetUnrealWorldTransform() const
{
	return RigidActor ? ToFTransform(RigidActor->getGlobalPose()) : FTransform();
}
