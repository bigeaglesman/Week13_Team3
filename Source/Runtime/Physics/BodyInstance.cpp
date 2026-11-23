#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXSDK.h"
#include "Physics/PhysicsFilter.h"
#include <cmath>

using namespace physx;

namespace
{
	constexpr float DefaultDensity = 1000.0f;   // kg/m³ (물). 1m 상자면 1000kg

	PxVec3 ToPx(const FVector& V) { return PxVec3(V.X, V.Y, V.Z); }
	PxQuat ToPx(const FQuat& Q) { return PxQuat(Q.X, Q.Y, Q.Z, Q.W); }

	float MaxAbsScale(const FVector& Scale)
	{
		return std::fmax(std::fabs(Scale.X), std::fmax(std::fabs(Scale.Y), std::fabs(Scale.Z)));
	}

	// 컴포넌트 스케일은 PhysX 액터에 넣을 수 없어서 각 도형 크기와 위치에 직접 곱한다.
	void CreateShapes(PxPhysics& Physics, PxMaterial& Material, PxRigidActor& Actor, const UBodySetup& Setup, const FVector& Scale, const PxFilterData& FilterData)
	{
		const PxVec3 PxScale = ToPx(Scale);

		auto Attach = [&](const auto& Geometry, const PxTransform& LocalPose)
		{
			if (!Geometry.isValid() || !LocalPose.isValid())
				return;

			PxShape* Shape = Physics.createShape(Geometry, Material, true);
			if (!Shape)
				return;

			Shape->setLocalPose(LocalPose);
			Shape->setSimulationFilterData(FilterData);
			Shape->setQueryFilterData(FilterData);
			Actor.attachShape(*Shape);
			Shape->release();   // 액터가 참조를 들고 있으므로 액터가 해제될 때 같이 해제된다
		};

		for (const FKSphereElem& Elem : Setup.AggGeom.SphereElems)
			Attach(PxSphereGeometry(Elem.Radius * MaxAbsScale(Scale)), PxTransform(ToPx(Elem.Center).multiply(PxScale)));

		for (const FKBoxElem& Elem : Setup.AggGeom.BoxElems)
		{
			const PxVec3 HalfExtent(0.5f * Elem.X * std::fabs(Scale.X), 0.5f * Elem.Y * std::fabs(Scale.Y), 0.5f * Elem.Z * std::fabs(Scale.Z));
			Attach(PxBoxGeometry(HalfExtent), PxTransform(ToPx(Elem.Center).multiply(PxScale), ToPx(Elem.Rotation.Quaternion())));
		}

		// PhysX 캡슐은 로컬 X축으로 누워 있고 UE Sphyl은 Z축으로 서 있다. Y축 -90도 회전으로 X축을 Z축에 맞춘다.
		const PxQuat SphylBasis(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f));
		for (const FKSphylElem& Elem : Setup.AggGeom.SphylElems)
		{
			const float RadiusScale = std::fmax(std::fabs(Scale.X), std::fabs(Scale.Y));
			const PxCapsuleGeometry Capsule(Elem.Radius * RadiusScale, 0.5f * Elem.Length * std::fabs(Scale.Z));
			Attach(Capsule, PxTransform(ToPx(Elem.Center).multiply(PxScale), ToPx(Elem.Rotation.Quaternion()) * SphylBasis));
		}
	}
}

void FBodyInstance::InitBody(UBodySetup* Setup, const FVector& Location, const FQuat& Rotation, const FVector& Scale, bool bSimulate, UPrimitiveComponent* InOwner, FPhysScene* InScene)
{
	TermBody();

	PxPhysics* Physics = PhysXSDK::Get();
	PxMaterial* Material = PhysXSDK::DefaultMaterial();
	if (!Setup || !InScene || !InScene->GetPxScene() || !Physics || !Material || Setup->AggGeom.GetElementCount() == 0)
		return;

	const PxTransform Pose(ToPx(Location), ToPx(Rotation).getNormalized());
	if (!Pose.isValid())
		return;

	if (bSimulate)
		RigidActor = Physics->createRigidDynamic(Pose);
	else
		RigidActor = Physics->createRigidStatic(Pose);

	if (!RigidActor)
		return;

	const EPhysicsObjectType Type = bSimulate ? EPhysicsObjectType::PhysicsBody : EPhysicsObjectType::WorldStatic;
	CreateShapes(*Physics, *Material, *RigidActor, *Setup, Scale, MakeFilterData(Type, 0xFFFFFFFFu));   // 지금은 모든 타입을 막는다
	if (RigidActor->getNbShapes() == 0)
	{
		TermBody();
		return;
	}

	if (PxRigidDynamic* Dynamic = RigidActor->is<PxRigidDynamic>())
		PxRigidBodyExt::updateMassAndInertia(*Dynamic, DefaultDensity);

	OwnerComponent = InOwner;
	Scene = InScene;
	RigidActor->userData = this;
	Scene->AddActor(RigidActor);
}

void FBodyInstance::TermBody()
{
	if (RigidActor)
	{
		RigidActor->userData = nullptr;
		RigidActor->release();   // 씬에 들어 있으면 씬에서도 빠진다
		RigidActor = nullptr;
	}
	OwnerComponent = nullptr;
	Scene = nullptr;
}

bool FBodyInstance::IsDynamic() const
{
	return RigidActor && RigidActor->is<PxRigidDynamic>() != nullptr;
}

FVector FBodyInstance::GetWorldLocation() const
{
	if (!RigidActor) return FVector(0.0f, 0.0f, 0.0f);
	const PxVec3 P = RigidActor->getGlobalPose().p;
	return FVector(P.x, P.y, P.z);
}

FQuat FBodyInstance::GetWorldRotation() const
{
	if (!RigidActor) return FQuat();
	const PxQuat Q = RigidActor->getGlobalPose().q;
	return FQuat(Q.x, Q.y, Q.z, Q.w);
}
