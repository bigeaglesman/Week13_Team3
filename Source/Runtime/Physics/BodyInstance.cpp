#include "EnginePCH.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/BodyInstance.h"
#include "Physics/AggregateGeom.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsCooking.h"
#include "Physics/PhysicalMaterial.h"

#include <PxPhysicsAPI.h>
#include "Physics/PhysXSceneLock.h"

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
	PxPhysics* Physics = Core.GetPhysics();

	// 재질: 지정한 UPhysicalMaterial이 있으면 그걸, 없거나 값이 잘못됐으면 엔진 기본 재질
	PxMaterial* Material = PhysMaterialOverride ? PhysMaterialOverride->GetPxMaterial() : nullptr;
	if (!Material)
	{
		Material = Core.GetDefaultMaterial();
	}

	// 시뮬레이션 여부와 상관없이 Dynamic으로 만든다. 안 움직이는 쪽은 키네마틱으로 둔다.
	// (PxRigidStatic은 옮길 때마다 브로드페이즈를 다시 짓고, 움직여도 다른 바디를 밀지 못한다.)
	PxRigidDynamic* Dynamic = Physics->createRigidDynamic(ToPx(WorldTransform));
	if (!Dynamic)
	{
		return;
	}

	// 필터 셰이더가 읽는 값. word0 = SelfCollisionGroup, word1 = 이벤트 플래그
	const PxFilterData FilterData(SelfCollisionGroup, bNotifyRigidBodyCollision ? FilterFlag_NotifyHit : 0u, 0, 0);

	// 아직 씬에 넣기 전이라 이 액터를 만지는 동안은 씬 락이 필요 없다.
	auto AddShape = [&](const PxGeometry& Geometry, const PxTransform& LocalPose)
	{
		// Exclusive = 이 액터 전용 (다른 액터와 공유하지 않음)
		if (PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Dynamic, Geometry, *Material))
		{
			Shape->setLocalPose(LocalPose);
			Shape->setSimulationFilterData(FilterData);
			Shape->setQueryFilterData(PxFilterData(InOwner->bIsVehicleDrivableSurface ? VehicleDrivableSurfaceQueryFlag : 0u, 0, 0, 0));
		}
	};

	for (const FKSphereElem& Elem : Geom.SphereElems)
	{
		AddShape(PxSphereGeometry(Elem.Radius), PxTransform(ToPx(Elem.Center)));
	}
	for (const FKBoxElem& Elem : Geom.BoxElems)
	{
		AddShape(PxBoxGeometry(ToPx(Elem.HalfExtent)), PxTransform(ToPx(Elem.Center), ToPx(Elem.Rotation).getNormalized()));
	}
	for (const FKSphylElem& Elem : Geom.SphylElems)
	{
		// PhysX halfHeight는 원기둥 부분의 절반
		AddShape(PxCapsuleGeometry(Elem.Radius, Elem.Length * 0.5f), PxTransform(ToPx(Elem.Center), ToPx(Elem.Rotation).getNormalized() * CapsuleXToZ));
	}
	for (const FKConvexElem& Elem : Geom.ConvexElems)
	{
		// 도형이 메시 참조를 하나 더 잡으므로, 붙인 뒤 우리 참조는 바로 놓는다.
		if (PxConvexMesh* Mesh = FPhysicsCooking::CookConvexMesh(Elem.VertexData))
		{
			AddShape(PxConvexMeshGeometry(Mesh), PxTransform(PxIdentity));
			Mesh->release();
		}
	}
	for (const FKTriMeshElem& Elem : Geom.TriMeshElems)
	{
		// PhysX는 시뮬레이션하는 Dynamic 바디에 삼각형 메시를 붙일 수 없다. 키네마틱(움직이지 않는 쪽)에만 붙인다.
		if (bSimulatePhysics)
		{
			HTR_LOG(Warning, "[PhysX] Triangle mesh collision is ignored on a simulating body. Use convex collision instead.");
			continue;
		}
		if (PxTriangleMesh* Mesh = FPhysicsCooking::CookTriangleMesh(Elem.Vertices, Elem.Indices))
		{
			AddShape(PxTriangleMeshGeometry(Mesh), PxTransform(PxIdentity));
			Mesh->release();
		}
	}

	if (Dynamic->getNbShapes() == 0)
	{
		Dynamic->release();
		return;
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

	// PxActor → FBodyInstance 역참조. 동기화와 충돌 이벤트에서 이걸로 컴포넌트를 찾는다.
	Dynamic->userData = this;
	{
		SCOPED_SCENE_WRITE_LOCK(Scene->GetPxScene());
		Scene->GetPxScene()->addActor(*Dynamic);
	}

	OwnerComponent = InOwner;
	RigidActor = Dynamic;
}

void FBodyInstance::TermBody()
{
	if (RigidActor)
	{
		// 씬이 먼저 해제됐으면 getScene()이 nullptr이다. 그때는 이벤트도 락도 없다.
		PxScene* Scene = RigidActor->getScene();

		// 아직 보내지 않은 충돌 이벤트가 이 바디를 가리키지 않게 한다.
		if (Scene && Scene->userData)
		{
			static_cast<FPhysicsScene*>(Scene->userData)->RemovePendingNotifies(this);
		}

		// release가 씬에서도 빼 준다.
		SCOPED_SCENE_WRITE_LOCK(Scene);
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
		SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
		Dynamic->setKinematicTarget(ToPx(WorldTransform));
	}
}

FTransform FBodyInstance::GetUnrealWorldTransform() const
{
	if (!RigidActor)
	{
		return FTransform();
	}

	SCOPED_SCENE_READ_LOCK(RigidActor->getScene());
	return ToFTransform(RigidActor->getGlobalPose());
}
