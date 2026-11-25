#include "EnginePCH.h"
#include "ClothingSystem/ClothingSimulationNv.h"
#include "ClothingSystem/ClothingSystemRuntimeModuleNv.h"
#include "ClothingSystem/ClothingAssetCommon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/AggregateGeom.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>

#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>
#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/PhaseConfig.h>
#include <NvClothExt/ClothFabricCooker.h>

using namespace physx;

namespace
{
	// 컴포넌트 월드 행렬 → NvCloth 로컬 프레임 (위치 + 회전, 스케일 없음)
	PxTransform GetSimulationFrame(const FClothingSimulationContextNv& Context)
	{
		return ToPxNoScale(Context.ComponentToWorld);
	}

	float GetMaxScale(const FVector& Scale)
	{
		return std::max({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z) });
	}

	const FClothConstraintSetupNv& GetConstraintSetup(const FClothConfigNv& Config, int32 PhaseType)
	{
		switch (PhaseType)
		{
		case nv::cloth::ClothFabricPhaseType::eVERTICAL:   return Config.VerticalConstraintConfig;
		case nv::cloth::ClothFabricPhaseType::eHORIZONTAL: return Config.HorizontalConstraintConfig;
		case nv::cloth::ClothFabricPhaseType::eBENDING:    return Config.BendConstraintConfig;
		default:                                           return Config.ShearConstraintConfig;
		}
	}
}

FClothingSimulationNv::~FClothingSimulationNv()
{
	Shutdown();
}

bool FClothingSimulationNv::Initialize()
{
	if (IsInitialized())
	{
		return true;
	}

	nv::cloth::Factory* Factory = FClothingSystemRuntimeModuleNv::Get().GetSoftwareFactory();
	if (!Factory)
	{
		return false;
	}

	Solver = Factory->createSolver();
	bFirstFrame = true;
	return Solver != nullptr;
}

void FClothingSimulationNv::Shutdown()
{
	// 천을 먼저 Solver에서 빼고 지운 다음 Solver를 지운다.
	DestroyActors();

	// NvCloth가 먼저 내려갔으면 (프로그램 종료 중 늦게 지워진 컴포넌트) Solver는 이미 쓸 수 없으니 버린다.
	if (Solver && FClothingSystemRuntimeModuleNv::Get().IsInitialized())
	{
		NV_CLOTH_DELETE(Solver);
	}
	Solver = nullptr;
}

void FClothingSimulationNv::FillContext(const USkeletalMeshComponent* Component, float DeltaTime, FClothingSimulationContextNv& OutContext) const
{
	OutContext.DeltaSeconds = std::min(DeltaTime, MaxDeltaSeconds);
	OutContext.ComponentToWorld = Component->GetWorldMatrix();
	OutContext.ComponentScale = Component->GetWorldScale3D();
	OutContext.BoneTransforms = Component->GetComponentSpaceTransform();
	OutContext.bTeleport = bFirstFrame;

	// 스키닝 행렬: 바인드 포즈 정점을 지금 포즈로 옮긴다. FSkeletalMeshObject::UpdateRefToLocalMatrices와 같은 식
	const TArray<FBoneInfo>& Bones = Component->GetBones();
	const int32 NumBones = std::min(Bones.Num(), OutContext.BoneTransforms.Num());
	OutContext.RefToLocals.SetNum(NumBones);
	for (int32 I = 0; I < NumBones; ++I)
	{
		OutContext.RefToLocals[I] = Bones[I].InverseBindPose * OutContext.BoneTransforms[I];
	}
}

bool FClothingSimulationNv::CreateActor(const UClothingAssetCommon* Asset, const UPhysicsAsset* PhysicsAsset, const FClothingSimulationContextNv& Context)
{
	nv::cloth::Factory* Factory = FClothingSystemRuntimeModuleNv::Get().GetSoftwareFactory();
	if (!Solver || !Factory || !Asset)
	{
		return false;
	}

	const FClothPhysicalMeshData& Mesh = Asset->PhysicalMeshData;
	const int32 NumVerts = Mesh.GetNumVerts();

	FClothingActorNv Actor;
	Actor.AssetCreatedFrom = Asset;

	// 1) 쿠킹 입력: 휴지 길이는 바인드 포즈 모양에 스케일을 곱한 것으로 잰다.
	TArray<PxVec3> RestPositions;
	RestPositions.SetNum(NumVerts);
	for (int32 V = 0; V < NumVerts; ++V)
	{
		RestPositions[V] = ToPx(Mesh.Vertices[V] * Context.ComponentScale);
	}

	nv::cloth::ClothMeshDesc Desc;
	Desc.points.data = RestPositions.GetData();
	Desc.points.stride = sizeof(PxVec3);
	Desc.points.count = static_cast<PxU32>(NumVerts);
	Desc.triangles.data = Mesh.Indices.GetData();
	Desc.triangles.stride = sizeof(uint32) * 3;
	Desc.triangles.count = static_cast<PxU32>(Mesh.Indices.Num() / 3);
	Desc.invMasses.data = Mesh.InverseMasses.GetData();
	Desc.invMasses.stride = sizeof(float);
	Desc.invMasses.count = static_cast<PxU32>(NumVerts);

	// 2) 쿠킹: 삼각형 연결을 보고 제약을 Phase 4종(세로·가로·굽힘·전단)으로 나눈다.
	//    세로/가로는 중력 방향으로 가르므로, 중력을 시뮬레이션 공간(컴포넌트 회전 기준)으로 돌려 넘긴다.
	const PxVec3 LocalGravity = GetSimulationFrame(Context).q.rotateInv(PxVec3(0.0f, 0.0f, WorldGravityZ));
	nv::cloth::Vector<int32_t>::Type PhaseTypes;
	Actor.Fabric = NvClothCookFabricFromMesh(Factory, Desc, LocalGravity, &PhaseTypes, true);
	if (!Actor.Fabric)
	{
		HTR_LOG(Warning, "[Cloth] Fabric cooking failed.");
		return false;
	}
	for (uint32 I = 0; I < PhaseTypes.size(); ++I)
	{
		Actor.PhaseTypes.Add(PhaseTypes[I]);
	}

	// 엔진의 삼각형 감김 순서(시계/반시계)에 따라 외적 방향이 메시 노멀과 반대일 수 있다. 바인드 포즈에서 한 번 확인한다.
	float NormalAgreement = 0.0f;
	for (int32 I = 0; I + 2 < Mesh.Indices.Num(); I += 3)
	{
		const uint32 A = Mesh.Indices[I], B = Mesh.Indices[I + 1], C = Mesh.Indices[I + 2];
		const FVector FaceNormal = FVector::Cross(Mesh.Vertices[B] - Mesh.Vertices[A], Mesh.Vertices[C] - Mesh.Vertices[A]);
		NormalAgreement += FaceNormal.Dot(Mesh.Normals[A] + Mesh.Normals[B] + Mesh.Normals[C]);
	}
	Actor.bFlipNormals = NormalAgreement < 0.0f;

	// 3) 입자: 지금 포즈 자리에서 시작한다. w는 역질량 (0 = 고정)
	SkinPhysicsMesh(Actor, Context);
	TArray<PxVec4> Particles;
	Particles.SetNum(NumVerts);
	for (int32 V = 0; V < NumVerts; ++V)
	{
		Particles[V] = PxVec4(ToPx(Actor.SkinnedPositions[V]), Mesh.InverseMasses[V]);
	}

	Actor.Cloth = Factory->createCloth(nv::cloth::Range<const PxVec4>(Particles.GetData(), Particles.GetData() + NumVerts), *Actor.Fabric);
	if (!Actor.Cloth)
	{
		Actor.Fabric->decRefCount();
		HTR_LOG(Warning, "[Cloth] createCloth failed.");
		return false;
	}

	// 4) 로컬 프레임을 지금 컴포넌트 자리에 맞추고 Solver에 넣는다.
	const PxTransform Frame = GetSimulationFrame(Context);
	Actor.Cloth->teleportToLocation(Frame.p, Frame.q);
	ApplyConfig(Actor, Context);
	Solver->addCloth(Actor.Cloth);

	// 5) 충돌체: Physics Asset의 구·캡슐 중 천 근처의 것
	ExtractPhysicsAssetCollisions(Actor, PhysicsAsset, Context);

	HTR_LOG(Info, "[Cloth] Actor created: {} particles, {} phases, {} collision spheres, {} capsules",
		NumVerts, Actor.PhaseTypes.Num(), Actor.Collisions.Spheres.Num(), Actor.Collisions.SphereConnections.Num());
	Actors.Add(std::move(Actor));
	bFirstFrame = true;
	return true;
}

void FClothingSimulationNv::DestroyActors()
{
	const bool bNvClothAlive = FClothingSystemRuntimeModuleNv::Get().IsInitialized();
	for (FClothingActorNv& Actor : Actors)
	{
		if (bNvClothAlive)
		{
			if (Actor.Cloth)
			{
				if (Solver)
				{
					Solver->removeCloth(Actor.Cloth);
				}
				NV_CLOTH_DELETE(Actor.Cloth);
			}
			if (Actor.Fabric)
			{
				Actor.Fabric->decRefCount();   // 쿠커가 준 참조를 돌려준다. 0이 되면 Fabric이 지워진다
			}
		}
		Actor.Cloth = nullptr;
		Actor.Fabric = nullptr;
	}
	Actors.Reset();
}

bool FClothingSimulationNv::HasActorForAsset(const UClothingAssetCommon* Asset) const
{
	for (const FClothingActorNv& Actor : Actors)
	{
		if (Actor.AssetCreatedFrom == Asset)
		{
			return true;
		}
	}
	return false;
}

void FClothingSimulationNv::SkinPhysicsMesh(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const
{
	const FClothPhysicalMeshData& Mesh = Actor.AssetCreatedFrom->PhysicalMeshData;
	const int32 NumVerts = Mesh.GetNumVerts();
	const int32 NumBones = Context.RefToLocals.Num();
	Actor.SkinnedPositions.SetNum(NumVerts);

	for (int32 V = 0; V < NumVerts; ++V)
	{
		const FClothVertBoneData& Bone = Mesh.BoneData[V];
		FVector Position = FVector::ZeroVector;
		float TotalWeight = 0.0f;
		for (int32 I = 0; I < Bone.NumInfluences; ++I)
		{
			if (Bone.BoneIndices[I] < NumBones)
			{
				Position += Context.RefToLocals[Bone.BoneIndices[I]].TransformPosition(Mesh.Vertices[V]) * Bone.BoneWeights[I];
				TotalWeight += Bone.BoneWeights[I];
			}
		}
		if (TotalWeight <= 0.0f)
		{
			Position = Mesh.Vertices[V];   // 뼈가 없으면 바인드 위치
		}

		// 컴포넌트 공간 → 시뮬레이션 공간 (회전·위치는 NvCloth 로컬 프레임이 처리하고, 스케일만 여기서 곱한다)
		Actor.SkinnedPositions[V] = Position * Context.ComponentScale;
	}
}

void FClothingSimulationNv::ApplyConfig(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const
{
	nv::cloth::Cloth* Cloth = Actor.Cloth;

	// Phase Constraint 4종: 쿠커가 알려 준 Phase 종류마다 설정을 고른다.
	TArray<nv::cloth::PhaseConfig> PhaseConfigs;
	for (int32 I = 0; I < Actor.PhaseTypes.Num(); ++I)
	{
		const FClothConstraintSetupNv& Setup = GetConstraintSetup(Config, Actor.PhaseTypes[I]);
		nv::cloth::PhaseConfig Phase(static_cast<uint16_t>(I));
		Phase.mStiffness = Setup.Stiffness;
		Phase.mStiffnessMultiplier = Setup.StiffnessMultiplier;
		Phase.mStretchLimit = Setup.StretchLimit;
		Phase.mCompressionLimit = Setup.CompressionLimit;
		PhaseConfigs.Add(Phase);
	}
	Cloth->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(PhaseConfigs.GetData(), PhaseConfigs.GetData() + PhaseConfigs.Num()));

	Cloth->setSolverFrequency(Config.SolverFrequency);
	Cloth->setStiffnessFrequency(Config.StiffnessFrequency);

	Cloth->setDamping(PxVec3(Config.Damping));
	Cloth->setLinearDrag(PxVec3(Config.LinearDrag));
	Cloth->setAngularDrag(PxVec3(Config.AngularDrag));
	Cloth->setLinearInertia(PxVec3(Config.LinearInertiaScale));
	Cloth->setAngularInertia(PxVec3(Config.AngularInertiaScale));
	Cloth->setCentrifugalInertia(PxVec3(Config.CentrifugalInertiaScale));

	Cloth->setTetherConstraintStiffness(Config.TetherStiffness);
	Cloth->setTetherConstraintScale(Config.TetherLimit);

	// 중력·바람은 NvCloth가 월드 좌표로 받는다 (로컬 프레임 회전은 NvCloth가 알아서 반영)
	Cloth->setGravity(PxVec3(0.0f, 0.0f, WorldGravityZ * Config.GravityScale));
	Cloth->setWindVelocity(ToPx(Context.WindVelocity + Config.WindVelocity));   // 컨텍스트(게임 쪽 바람) + 설정값
	Cloth->setDragCoefficient(Config.WindDragCoefficient);
	Cloth->setLiftCoefficient(Config.WindLiftCoefficient);

	Cloth->setFriction(Config.Friction);
}

void FClothingSimulationNv::UpdateConstraints(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const
{
	const FClothPhysicalMeshData& Mesh = Actor.AssetCreatedFrom->PhysicalMeshData;
	const int32 NumVerts = Mesh.GetNumVerts();
	const float DistanceScale = Config.MaxDistanceScale * GetMaxScale(Context.ComponentScale);

	// Max Distance: 입자마다 "스키닝 위치를 중심, Max Distance를 반지름"으로 하는 구. 입자는 이 구 밖으로 못 나간다.
	// NvCloth는 범위 전체를 써야 한다고 요구한다.
	nv::cloth::Range<PxVec4> MotionConstraints = Actor.Cloth->getMotionConstraints();
	for (int32 V = 0; V < NumVerts; ++V)
	{
		MotionConstraints[V] = PxVec4(ToPx(Actor.SkinnedPositions[V]), Mesh.MaxDistances[V] * DistanceScale);
	}

	// 고정 입자(역질량 0)는 솔버가 움직이지 않으므로 직접 스키닝 위치로 옮긴다.
	nv::cloth::MappedRange<PxVec4> Particles = Actor.Cloth->getCurrentParticles();
	for (int32 V = 0; V < NumVerts; ++V)
	{
		if (Mesh.InverseMasses[V] == 0.0f)
		{
			Particles[V] = PxVec4(ToPx(Actor.SkinnedPositions[V]), 0.0f);
		}
	}
}

void FClothingSimulationNv::Simulate(const FClothingSimulationContextNv& Context)
{
	if (!Solver || Actors.Num() == 0 || Context.DeltaSeconds <= 0.0f)
	{
		return;
	}

	const PxTransform Frame = GetSimulationFrame(Context);

	for (FClothingActorNv& Actor : Actors)
	{
		SkinPhysicsMesh(Actor, Context);
		ApplyConfig(Actor, Context);

		if (Context.bTeleport)
		{
			// 순간이동: 관성 없이 새 자리로 옮기고, 모든 입자를 지금 포즈 자리로 되돌린다.
			Actor.Cloth->teleportToLocation(Frame.p, Frame.q);
			const TArray<float>& InverseMasses = Actor.AssetCreatedFrom->PhysicalMeshData.InverseMasses;
			{
				nv::cloth::MappedRange<PxVec4> Current = Actor.Cloth->getCurrentParticles();
				nv::cloth::MappedRange<PxVec4> Previous = Actor.Cloth->getPreviousParticles();
				for (int32 V = 0; V < Actor.SkinnedPositions.Num(); ++V)
				{
					Current[V] = Previous[V] = PxVec4(ToPx(Actor.SkinnedPositions[V]), InverseMasses[V]);
				}
			}
		}
		else
		{
			// 평소: 로컬 프레임을 옮기면 NvCloth가 그 이동·회전으로 생기는 관성(Inertia Scale만큼)을 입자에 준다.
			Actor.Cloth->setTranslation(Frame.p);
			Actor.Cloth->setRotation(Frame.q);
		}

		UpdateConstraints(Actor, Context);
		UpdateCollisions(Actor, Context);

		// NvCloth는 Max Distance 구와 충돌체를 지난 프레임 → 이번 프레임으로 보간하며 푼다.
		// 순간이동이면 지난 프레임 값이 엉뚱한 자리라서, 방금 넣은 값으로 시작과 끝을 맞춘다.
		if (Context.bTeleport)
		{
			Actor.Cloth->clearInterpolation();
		}
	}

	// NvCloth는 일을 chunk로 나눠 준다. 지금은 한 스레드에서 차례로 돌린다.
	// (나중에 chunk마다 작업 스레드에 나눠 주면 그대로 병렬이 된다)
	if (Solver->beginSimulation(Context.DeltaSeconds))
	{
		const int ChunkCount = Solver->getSimulationChunkCount();
		for (int ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			Solver->simulateChunk(ChunkIndex);
		}
		Solver->endSimulation();
	}

	for (FClothingActorNv& Actor : Actors)
	{
		ReadSimulationResults(Actor, Context);
	}

	bFirstFrame = false;
}

void FClothingSimulationNv::ExtractPhysicsAssetCollisions(FClothingActorNv& Actor, const UPhysicsAsset* PhysicsAsset, const FClothingSimulationContextNv& Context) const
{
	Actor.Collisions.Reset();
	const USkeletalMesh* Mesh = Actor.AssetCreatedFrom->OwningMesh;
	if (!PhysicsAsset || !Mesh || Actor.SkinnedPositions.IsEmpty())
	{
		return;
	}

	// 천의 지금 자리(시뮬레이션 공간) 바운드. 여기서 가까운 충돌체부터 고른다.
	FVector BoundsMin(FLT_MAX, FLT_MAX, FLT_MAX), BoundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const FVector& P : Actor.SkinnedPositions)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			BoundsMin[Axis] = std::min(BoundsMin[Axis], P[Axis]);
			BoundsMax[Axis] = std::max(BoundsMax[Axis], P[Axis]);
		}
	}

	const float MaxScale = GetMaxScale(Context.ComponentScale);
	auto DistanceToCloth = [&](const FClothCollisionPrim_Sphere& Sphere)
	{
		const FVector Center = Context.BoneTransforms[Sphere.BoneIndex].TransformPosition(Sphere.LocalPosition) * Context.ComponentScale;
		float DistSq = 0.0f;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float D = std::max({ BoundsMin[Axis] - Center[Axis], 0.0f, Center[Axis] - BoundsMax[Axis] });
			DistSq += D * D;
		}
		return std::max(0.0f, std::sqrt(DistSq) - Sphere.Radius * MaxScale);
	};

	// 후보: 구는 구 1개, 캡슐(Sphyl)은 양 끝 구 2개 + 연결 1개
	struct FCandidate
	{
		float Distance = 0.0f;
		int32 NumSpheres = 1;
		FClothCollisionPrim_Sphere Spheres[2];
	};
	TArray<FCandidate> Candidates;

	for (const UBodySetup* Setup : PhysicsAsset->BodySetups)
	{
		const int32 BoneIndex = Setup ? Mesh->FindBoneIndex(Setup->BoneName) : -1;
		if (BoneIndex < 0 || BoneIndex >= Context.BoneTransforms.Num())
		{
			continue;
		}

		for (const FKSphereElem& Elem : Setup->AggGeom.SphereElems)
		{
			FCandidate C;
			C.Spheres[0] = { BoneIndex, Elem.Radius, Elem.Center };
			C.Distance = DistanceToCloth(C.Spheres[0]);
			Candidates.Add(C);
		}

		for (const FKSphylElem& Elem : Setup->AggGeom.SphylElems)
		{
			// 캡슐 축은 로컬 Z. 원기둥 길이의 절반만큼 양쪽으로 간 곳이 양 끝 구의 중심이다.
			const FVector HalfAxis = Elem.Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f)) * (Elem.Length * 0.5f);
			FCandidate C;
			C.NumSpheres = 2;
			C.Spheres[0] = { BoneIndex, Elem.Radius, Elem.Center + HalfAxis };
			C.Spheres[1] = { BoneIndex, Elem.Radius, Elem.Center - HalfAxis };
			C.Distance = std::min(DistanceToCloth(C.Spheres[0]), DistanceToCloth(C.Spheres[1]));
			Candidates.Add(C);
		}
	}

	// NvCloth 한도(구 32, 캡슐 32) 안에서 천에 가까운 것부터 넣는다.
	std::sort(Candidates.begin(), Candidates.end(), [](const FCandidate& A, const FCandidate& B) { return A.Distance < B.Distance; });
	for (const FCandidate& C : Candidates)
	{
		if (Actor.Collisions.Spheres.Num() + C.NumSpheres > MaxCollisionSpheres)
		{
			continue;
		}
		if (C.NumSpheres == 2 && Actor.Collisions.SphereConnections.Num() >= MaxCollisionCapsules)
		{
			continue;
		}

		const int32 First = Actor.Collisions.Spheres.Num();
		for (int32 I = 0; I < C.NumSpheres; ++I)
		{
			Actor.Collisions.Spheres.Add(C.Spheres[I]);
		}
		if (C.NumSpheres == 2)
		{
			FClothCollisionPrim_SphereConnection Connection;
			Connection.SphereIndices[0] = First;
			Connection.SphereIndices[1] = First + 1;
			Actor.Collisions.SphereConnections.Add(Connection);
		}
	}
}

void FClothingSimulationNv::UpdateCollisions(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const
{
	nv::cloth::Cloth* Cloth = Actor.Cloth;
	const FClothCollisionData& Collisions = Actor.Collisions;

	// 끄면 넣어 둔 충돌체를 모두 뺀다.
	if (!Config.bEnableCollision || Collisions.Spheres.IsEmpty())
	{
		if (Cloth->getNumCapsules() > 0)
		{
			Cloth->setCapsules(nv::cloth::Range<const uint32_t>(), 0, Cloth->getNumCapsules());
		}
		if (Cloth->getNumSpheres() > 0)
		{
			Cloth->setSpheres(nv::cloth::Range<const PxVec4>(), 0, Cloth->getNumSpheres());
		}
		return;
	}

	// 구: 뼈의 이번 프레임 행렬로 중심을 옮기고, 시뮬레이션 공간(스케일 적용)으로 바꾼다.
	const float MaxScale = GetMaxScale(Context.ComponentScale);
	TArray<PxVec4> Spheres;
	Spheres.SetNum(Collisions.Spheres.Num());
	for (int32 I = 0; I < Collisions.Spheres.Num(); ++I)
	{
		const FClothCollisionPrim_Sphere& Sphere = Collisions.Spheres[I];
		const FVector Center = Sphere.BoneIndex < Context.BoneTransforms.Num()
			? Context.BoneTransforms[Sphere.BoneIndex].TransformPosition(Sphere.LocalPosition)
			: Sphere.LocalPosition;
		Spheres[I] = PxVec4(ToPx(Center * Context.ComponentScale), Sphere.Radius * MaxScale + Config.CollisionThickness);
	}
	// 기존 구 [0, 이전 개수)를 새 목록으로 바꾼다. NvCloth가 지난 프레임 자리 → 이번 자리로 보간한다.
	Cloth->setSpheres(nv::cloth::Range<const PxVec4>(Spheres.GetData(), Spheres.GetData() + Spheres.Num()), 0, Cloth->getNumSpheres());

	// 캡슐: 구 번호 두 개씩
	TArray<uint32_t> Capsules;
	for (const FClothCollisionPrim_SphereConnection& Connection : Collisions.SphereConnections)
	{
		Capsules.Add(static_cast<uint32_t>(Connection.SphereIndices[0]));
		Capsules.Add(static_cast<uint32_t>(Connection.SphereIndices[1]));
	}
	Cloth->setCapsules(nv::cloth::Range<const uint32_t>(Capsules.GetData(), Capsules.GetData() + Capsules.Num()), 0, Cloth->getNumCapsules());
}

void FClothingSimulationNv::GetSimulationData(TArray<FClothSimulData>& OutData, const USkeletalMesh* Mesh) const
{
	OutData.Reset();
	if (!Mesh)
	{
		return;
	}

	const TArray<UClothingAssetCommon*>& Assets = Mesh->GetMeshClothingAssets();
	OutData.SetNum(Assets.Num());
	for (const FClothingActorNv& Actor : Actors)
	{
		const int32 AssetIndex = Assets.Find(const_cast<UClothingAssetCommon*>(Actor.AssetCreatedFrom));
		if (AssetIndex >= 0 && !Actor.SimPositions.IsEmpty())
		{
			OutData[AssetIndex].Positions = Actor.SimPositions;
			OutData[AssetIndex].Normals = Actor.SimNormals;
		}
	}
}

void FClothingSimulationNv::ReadSimulationResults(FClothingActorNv& Actor, const FClothingSimulationContextNv& Context) const
{
	const FClothPhysicalMeshData& Mesh = Actor.AssetCreatedFrom->PhysicalMeshData;
	const int32 NumVerts = Mesh.GetNumVerts();
	const FVector& Scale = Context.ComponentScale;
	const FVector InvScale(
		Scale.X != 0.0f ? 1.0f / Scale.X : 0.0f,
		Scale.Y != 0.0f ? 1.0f / Scale.Y : 0.0f,
		Scale.Z != 0.0f ? 1.0f / Scale.Z : 0.0f);

	// 위치: 시뮬레이션 공간 → 컴포넌트 공간 (스케일을 되돌린다)
	Actor.SimPositions.SetNum(NumVerts);
	{
		const nv::cloth::Cloth* ConstCloth = Actor.Cloth;
		nv::cloth::MappedRange<const PxVec4> Particles = ConstCloth->getCurrentParticles();
		for (int32 V = 0; V < NumVerts; ++V)
		{
			Actor.SimPositions[V] = FVector(Particles[V].x, Particles[V].y, Particles[V].z) * InvScale;
		}
	}

	// 노멀: 삼각형 면 노멀을 꼭짓점에 더해 평균 낸다 (넓은 삼각형일수록 많이 반영된다)
	Actor.SimNormals.Init(FVector::ZeroVector, NumVerts);
	for (int32 I = 0; I + 2 < Mesh.Indices.Num(); I += 3)
	{
		const uint32 A = Mesh.Indices[I], B = Mesh.Indices[I + 1], C = Mesh.Indices[I + 2];
		FVector FaceNormal = FVector::Cross(Actor.SimPositions[B] - Actor.SimPositions[A], Actor.SimPositions[C] - Actor.SimPositions[A]);
		if (Actor.bFlipNormals)
		{
			FaceNormal = FaceNormal * -1.0f;
		}
		Actor.SimNormals[A] += FaceNormal;
		Actor.SimNormals[B] += FaceNormal;
		Actor.SimNormals[C] += FaceNormal;
	}
	for (int32 V = 0; V < NumVerts; ++V)
	{
		Actor.SimNormals[V] = Actor.SimNormals[V].GetSafeNormal(SMALL_NUMBER, Mesh.Normals[V]);
	}
}
