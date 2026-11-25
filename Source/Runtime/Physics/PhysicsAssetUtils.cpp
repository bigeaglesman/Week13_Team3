#include "EnginePCH.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysXConversion.h"
#include "Render/SkeletalMesh.h"

namespace
{
	constexpr float HalfPi = 1.57079632679f;
	constexpr float Pi = 3.14159265359f;

	struct FBoneVertexBounds
	{
		FVector Min = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector Max = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		int32 NumVertices = 0;

		void Add(const FVector& P)
		{
			Min = Min.ComponentMin(P);
			Max = Max.ComponentMax(P);
			++NumVertices;
		}
	};

	int32 DominantBone(const FSkinWeight& Weight)
	{
		int32 Bone = -1;
		float BestWeight = 0.0f;
		for (int32 k = 0; k < MaxBoneInfluences; ++k)
		{
			if (Weight.Weight[k] > BestWeight)
			{
				BestWeight = Weight.Weight[k];
				Bone = Weight.BoneIndex[k];
			}
		}
		return Bone;
	}

	FMatrix RigidBindMatrix(const FBoneInfo& Bone)
	{
		return ToFTransform(ToPxNoScale(Bone.InverseBindPose.Inverse())).GetLocalMatrix();
	}

	float GetAxis(const FVector& V, int32 Axis)
	{
		return Axis == 0 ? V.X : (Axis == 1 ? V.Y : V.Z);
	}

	int32 GetLongestAxis(const FVector& V)
	{
		if (V.X >= V.Y && V.X >= V.Z) return 0;
		return V.Y >= V.Z ? 1 : 2;
	}

	// 로컬 Z를 Axis 방향으로 돌린다. 캡슐(Sphyl) 축이 Z라서 쓴다.
	FQuat RotateZToAxis(int32 Axis)
	{
		if (Axis == 0) return FQuat::MakeFromAxisAngle(FVector(0, 1, 0), HalfPi);    // Z → X
		if (Axis == 1) return FQuat::MakeFromAxisAngle(FVector(1, 0, 0), -HalfPi);   // Z → Y
		return FQuat::Identity();
	}

	// 로컬 X를 Axis 방향으로 돌린다. 관절 Twist 축이 X라서 쓴다.
	FQuat RotateXToAxis(int32 Axis)
	{
		if (Axis == 1) return FQuat::MakeFromAxisAngle(FVector(0, 0, 1), HalfPi);    // X → Y
		if (Axis == 2) return FQuat::MakeFromAxisAngle(FVector(0, 1, 0), -HalfPi);   // X → Z
		return FQuat::Identity();
	}

	FKSphylElem FitCapsule(const FBoneVertexBounds& Bounds, const FVector& Extent, int32 Long, float MinRadius)
	{
		const float Radius = std::max(GetAxis(Extent, (Long + 1) % 3), GetAxis(Extent, (Long + 2) % 3));
		const float HalfHeight = std::max(GetAxis(Extent, Long), Radius);
		FKSphylElem Elem;
		Elem.Center = (Bounds.Max + Bounds.Min) * 0.5f;
		Elem.Rotation = RotateZToAxis(Long);
		Elem.Radius = std::max(Radius, MinRadius);
		Elem.Length = 2.0f * std::max(HalfHeight - Elem.Radius, 0.0f);
		return Elem;
	}

	void AppendBody(UPhysicsAsset& Asset, const FString& BoneName, const FKSphylElem& Capsule)
	{
		UBodySetup* Setup = NewObject<UBodySetup>(&Asset);
		Setup->BoneName = BoneName;
		Setup->AggGeom.SphylElems.Add(Capsule);
		Setup->DefaultInstance.bSimulatePhysics = true;
		Asset.BodySetups.Add(Setup);
	}
}

UPhysicsAsset* FPhysicsAssetUtils::CreateFromSkeletalMesh(const USkeletalMesh* Mesh, const FPhysAssetCreateParams& Params)
{
	if (!Mesh)
	{
		return nullptr;
	}

	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	const FSkeletalMeshRenderData& RenderData = Mesh->GetRenderData();
	if (Bones.IsEmpty() || RenderData.LODRenderData.IsEmpty())
	{
		return nullptr;
	}

	const FSkeletalMeshLODRenderData& LOD = RenderData.LODRenderData[0];
	const int32 NumBones = Bones.Num();

	// 바인드 포즈의 뼈 프레임에서 스케일을 뺀다. 런타임 바디 포즈도 뼈 월드 행렬에서 스케일을 뺀 값이라 기준이 같아진다.
	// 뼈에 스케일이 들어 있는 메시(FBX 단위 변환 등)에서 InverseBindPose를 그대로 쓰면 크기가 그 배율만큼 줄어든다.
	TArray<FMatrix> BindNoScale;      // 뼈 로컬(스케일 없음) → 컴포넌트
	TArray<FMatrix> InvBindNoScale;   // 컴포넌트 → 뼈 로컬(스케일 없음)
	BindNoScale.SetNum(NumBones);
	InvBindNoScale.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		BindNoScale[i] = RigidBindMatrix(Bones[i]);
		InvBindNoScale[i] = BindNoScale[i].Inverse();
	}

	// ---- 1. 뼈마다 담당 정점의 로컬 AABB ----
	// 정점은 가중치가 가장 큰 뼈 하나에만 넣는다 (UE의 EVW_DominantWeight). 크기는 메시 단위다.
	TArray<FBoneVertexBounds> Bounds;
	Bounds.SetNum(NumBones);

	const int32 NumVertices = std::min(LOD.BindVertices.Num(), LOD.SkinWeights.Num());
	for (int32 v = 0; v < NumVertices; ++v)
	{
		const int32 Dominant = DominantBone(LOD.SkinWeights[v]);
		if (Dominant < 0 || Dominant >= NumBones)
		{
			continue;
		}

		const FVector P = InvBindNoScale[Dominant].TransformPosition(LOD.BindVertices[v].Position);
		Bounds[Dominant].Add(P);
	}

	// ---- 2. 뼈마다 캡슐 바디 ----
	const FVector MeshSize = LOD.BindAABB.Max - LOD.BindAABB.Min;
	const float MinBoneSize = std::max({ MeshSize.X, MeshSize.Y, MeshSize.Z }) * Params.MinBoneSizeRatio;

	UPhysicsAsset* Asset = NewObject<UPhysicsAsset>();

	TArray<int32> BoneToBody;     // 뼈 인덱스 → 바디 인덱스 (-1 = 바디 없음)
	BoneToBody.Init(-1, NumBones);
	TArray<int32> BodyLongAxis;   // 바디 인덱스 → 캡슐 축 (0=X, 1=Y, 2=Z, 뼈 로컬)
	TArray<float> BodyVolume;

	for (int32 i = 0; i < NumBones; ++i)
	{
		const FBoneVertexBounds& B = Bounds[i];
		if (B.NumVertices == 0)
		{
			continue;
		}

		const FVector Extent = (B.Max - B.Min) * 0.5f;
		const int32 Long = GetLongestAxis(Extent);
		if (GetAxis(Extent, Long) * 2.0f < MinBoneSize)
		{
			continue;
		}

		const FKSphylElem Elem = FitCapsule(B, Extent, Long, MinBoneSize * 0.25f);
		BoneToBody[i] = Asset->BodySetups.Num();
		AppendBody(*Asset, Bones[i].Name, Elem);
		BodyLongAxis.Add(Long);
		BodyVolume.Add(Pi * Elem.Radius * Elem.Radius * (Elem.Length + 4.0f / 3.0f * Elem.Radius));
	}

	if (Asset->BodySetups.IsEmpty())
	{
		delete Asset;
		return nullptr;
	}

	// ---- 3. 질량: 총 질량을 부피 비율로 나눈다 ----
	float TotalVolume = 0.0f;
	for (float Volume : BodyVolume)
	{
		TotalVolume += Volume;
	}
	for (int32 b = 0; b < Asset->BodySetups.Num(); ++b)
	{
		Asset->BodySetups[b]->DefaultInstance.MassInKg = Params.TotalMassKg * BodyVolume[b] / TotalVolume;
	}

	// ---- 4. 관절: 바디가 있는 가장 가까운 조상과 잇는다 ----
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 ChildBody = BoneToBody[i];
		if (ChildBody < 0)
		{
			continue;
		}

		int32 Parent = Bones[i].ParentIndex;
		while (Parent >= 0 && BoneToBody[Parent] < 0)
		{
			Parent = Bones[Parent].ParentIndex;
		}
		if (Parent < 0)
		{
			continue;   // 루트 바디
		}

		FConstraintSetup Constraint;
		Constraint.JointName = Bones[i].Name;
		Constraint.ChildBoneName = Bones[i].Name;
		Constraint.ParentBoneName = Bones[Parent].Name;

		// 자식 뼈 원점에서, X(Twist)를 캡슐 축에 맞춘다.
		Constraint.ChildFrame = FTransform(RotateXToAxis(BodyLongAxis[ChildBody]), FVector(0, 0, 0));

		// 같은 프레임을 부모 뼈 로컬로 옮긴다: 자식 로컬 → 컴포넌트(바인드) → 부모 로컬
		const FMatrix FrameInParent = Constraint.ChildFrame.GetLocalMatrix() * BindNoScale[i] * InvBindNoScale[Parent];
		Constraint.ParentFrame = ToFTransform(ToPxNoScale(FrameInParent));

		Constraint.TwistLimitDeg = Params.TwistLimitDeg;
		Constraint.Swing1LimitDeg = Params.Swing1LimitDeg;
		Constraint.Swing2LimitDeg = Params.Swing2LimitDeg;

		Asset->ConstraintSetups.Add(Constraint);
	}

	return Asset;
}

bool FPhysicsAssetUtils::AddCapsuleBodyForBone(USkeletalMesh& Mesh, int32 BoneIndex)
{
	const auto& Bones = Mesh.GetRefSkeleton();
	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (!Bones.IsValidIndex(BoneIndex) || LODs.IsEmpty()) return false;

	const FBoneInfo& Bone = Bones[BoneIndex];
	UPhysicsAsset* Asset = Mesh.GetPhysicsAsset();
	const int32 BodyIndex = Asset ? Asset->FindBodyIndex(Bone.Name) : -1;
	UBodySetup* Existing = BodyIndex >= 0 ? Asset->BodySetups[BodyIndex] : nullptr;
	if (Existing && !Existing->AggGeom.IsEmpty()) return false;

	const auto& LOD = LODs[0];
	const FMatrix ToBone = RigidBindMatrix(Bone).Inverse();
	FBoneVertexBounds Bounds;
	const int32 NumVertices = std::min(LOD.BindVertices.Num(), LOD.SkinWeights.Num());
	for (int32 i = 0; i < NumVertices; ++i)
	{
		if (DominantBone(LOD.SkinWeights[i]) == BoneIndex)
		{
			Bounds.Add(ToBone.TransformPosition(LOD.BindVertices[i].Position));
		}
	}

	// 담당 정점이 없는 끝 본도 편집할 수 있도록 본 원점에 작은 캡슐을 둔다.
	FKSphylElem Capsule;
	const FVector MeshSize = LOD.BindAABB.Max - LOD.BindAABB.Min;
	const float MeshLength = std::max({ MeshSize.X, MeshSize.Y, MeshSize.Z });
	if (MeshLength > 0.0f)
	{
		Capsule.Radius = MeshLength * FPhysAssetCreateParams{}.MinBoneSizeRatio * 0.25f;
		Capsule.Length = 2.0f * Capsule.Radius;
	}
	if (Bounds.NumVertices > 0)
	{
		const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		const int32 Axis = GetLongestAxis(Extent);
		const float HalfLength = GetAxis(Extent, Axis);
		if (HalfLength > 0.0f)
		{
			Capsule = FitCapsule(Bounds, Extent, Axis, std::min(Capsule.Radius, HalfLength * 0.25f));
		}
	}

	if (Existing)
	{
		Existing->AggGeom.SphylElems.Add(Capsule);
	}
	else
	{
		if (!Asset)
		{
			Asset = NewObject<UPhysicsAsset>();
			Mesh.SetPhysicsAsset(Asset);
		}
		AppendBody(*Asset, Bone.Name, Capsule);
	}
	return true;
}
