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
	constexpr float MinPrimitiveExtentMeters = 0.005f; // UE4.27 MinPrimSize = 0.5 cm

	struct FBoneVertices
	{
		FVector Min = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector Max = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		TArray<FVector> Points;

		void Add(const FVector& P)
		{
			Min = Min.ComponentMin(P);
			Max = Max.ComponentMax(P);
			Points.Add(P);
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

	FQuat CapsuleOrientationInBox(int32 Axis)
	{
		if (Axis == 0) return FQuat::MakeFromAxisAngle(FVector(0, 1, 0), -HalfPi);
		if (Axis == 1) return FQuat::MakeFromAxisAngle(FVector(1, 0, 0), HalfPi);
		return FQuat::Identity();
	}

	FQuat FitBoneOrientation(const TArray<FVector>& Points)
	{
		if (Points.Num() < 2) return FQuat::Identity();
		FVector Mean(0);
		for (const FVector& P : Points) Mean += P;
		Mean /= static_cast<float>(Points.Num());

		// UE의 Auto Orient와 같이 공분산 행렬의 주축을 구해 도형의 로컬 Z로 쓴다.
		FVector Rows[3] = { FVector(0), FVector(0), FVector(0) };
		for (const FVector& P : Points)
		{
			const FVector Offset = P - Mean;
			for (int32 Axis = 0; Axis < 3; ++Axis)
				Rows[Axis] += Offset * Offset.Component(Axis);
		}
		FVector Z(0, 0, 1);
		for (int32 i = 0; i < 32; ++i)
		{
			const FVector Next(Rows[0].Dot(Z), Rows[1].Dot(Z), Rows[2].Dot(Z));
			const float Length = Next.Size();
			if (Length == 0.0f) return FQuat::Identity();
			Z = Next / Length;
		}
		const FVector AbsZ = Z.GetAbs();
		const FVector Seed = AbsZ.Z > AbsZ.X && AbsZ.Z > AbsZ.Y ? FVector(1, 0, 0) : FVector(0, 0, 1);
		const FVector Y = (Seed - Z * Seed.Dot(Z)).GetSafeNormal();
		FMatrix Basis = FMatrix::Identity;
		Basis.SetAxis(0, Y.Cross(Z));
		Basis.SetAxis(1, Y);
		Basis.SetAxis(2, Z);
		return ToFQuat(ToPxNoScale(Basis).q);
	}

	FKSphylElem FitCapsule(const FBoneVertices& Vertices, const FMatrix& BoneBind, float MinExtent)
	{
		const FQuat Orientation = FitBoneOrientation(Vertices.Points);
		FVector Extent(MinExtent);
		FKSphylElem Elem;
		if (!Vertices.Points.IsEmpty())
		{
			const FQuat ToElement = Orientation.UnitInverse();
			FBox Box{ FVector(FLT_MAX), FVector(-FLT_MAX) };
			for (const FVector& P : Vertices.Points)
			{
				const FVector Local = ToElement.RotateVector(P);
				Box.Min = Box.Min.ComponentMin(Local);
				Box.Max = Box.Max.ComponentMax(Local);
			}
			Elem.Center = Orientation.RotateVector((Box.Max + Box.Min) * 0.5f);
			const FBox BindBox = Box.GetWorldAABB(BoneBind);
			const FVector BindExtent = (BindBox.Max - BindBox.Min) * 0.5f;
			if (std::min({ BindExtent.X, BindExtent.Y, BindExtent.Z }) >= MinExtent)
				Extent = (Box.Max - Box.Min) * 0.5f;
		}
		// UE4.27은 동률이면 Z를 쓰고, 반폭과 원기둥 길이에 각각 1% 여유를 준다.
		int32 Long = 2;
		if (Extent.X > Extent.Y && Extent.X > Extent.Z) Long = 0;
		else if (Extent.Y > Extent.X && Extent.Y > Extent.Z) Long = 1;
		Elem.Rotation = Orientation * CapsuleOrientationInBox(Long);
		const float Radius = std::max(GetAxis(Extent, (Long + 1) % 3), GetAxis(Extent, (Long + 2) % 3));
		Elem.Radius = 1.01f * (Radius > 0.0f ? Radius : MinExtent);
		Elem.Length = 1.01f * GetAxis(Extent, Long);
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

	FConstraintSetup MakeBoneConstraint(const FString& ChildName, const FString& ParentName, const FQuat& CapsuleRotation,
		const FMatrix& ChildBind, const FMatrix& InverseParentBind, const FPhysAssetCreateParams& Params)
	{
		FConstraintSetup Constraint;
		Constraint.JointName = ChildName;
		Constraint.ChildBoneName = ChildName;
		Constraint.ParentBoneName = ParentName;
		// 자식 본 원점에 피벗을 두고, 캡슐 축이 Twist가 되도록 같은 프레임을 양쪽 본 로컬로 표현한다.
		const FQuat TwistToCapsule = FQuat::MakeFromAxisAngle(FVector(0, 1, 0), -HalfPi);
		Constraint.ChildFrame = FTransform(CapsuleRotation * TwistToCapsule, FVector(0, 0, 0));
		const FMatrix FrameInParent = Constraint.ChildFrame.GetLocalMatrix() * ChildBind * InverseParentBind;
		Constraint.ParentFrame = ToFTransform(ToPxNoScale(FrameInParent));
		Constraint.TwistLimitDeg = Params.TwistLimitDeg;
		Constraint.Swing1LimitDeg = Params.Swing1LimitDeg;
		Constraint.Swing2LimitDeg = Params.Swing2LimitDeg;
		return Constraint;
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
	TArray<FBoneVertices> Bounds;
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
	const float MinExtent = MinPrimitiveExtentMeters / Mesh->GetMetersPerAssetUnit();
	TArray<float> BodyVolume;

	for (int32 i = 0; i < NumBones; ++i)
	{
		const FBoneVertices& B = Bounds[i];
		if (B.Points.IsEmpty())
		{
			continue;
		}

		const FVector Extent = (B.Max - B.Min) * 0.5f;
		const int32 Long = GetLongestAxis(Extent);
		if (GetAxis(Extent, Long) * 2.0f < MinBoneSize)
		{
			continue;
		}

		const FKSphylElem Elem = FitCapsule(B, BindNoScale[i], MinExtent);
		BoneToBody[i] = Asset->BodySetups.Num();
		AppendBody(*Asset, Bones[i].Name, Elem);
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

		const FQuat& Rotation = Asset->BodySetups[ChildBody]->AggGeom.SphylElems[0].Rotation;
		Asset->ConstraintSetups.Add(MakeBoneConstraint(Bones[i].Name, Bones[Parent].Name, Rotation,
			BindNoScale[i], InvBindNoScale[Parent], Params));
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
	const FMatrix ChildBind = RigidBindMatrix(Bone);
	const FMatrix ToBone = ChildBind.Inverse();
	FBoneVertices Vertices;
	const int32 NumVertices = std::min(LOD.BindVertices.Num(), LOD.SkinWeights.Num());
	for (int32 i = 0; i < NumVertices; ++i)
	{
		if (DominantBone(LOD.SkinWeights[i]) == BoneIndex)
		{
			Vertices.Add(ToBone.TransformPosition(LOD.BindVertices[i].Position));
		}
	}

	const FKSphylElem Capsule = FitCapsule(Vertices, ChildBind, MinPrimitiveExtentMeters / Mesh.GetMetersPerAssetUnit());
	const FPhysAssetCreateParams Params;

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

	for (int32 Parent = Bone.ParentIndex; Bones.IsValidIndex(Parent); Parent = Bones[Parent].ParentIndex)
	{
		const FBoneInfo& ParentBone = Bones[Parent];
		const int32 ParentBody = Asset->FindBodyIndex(ParentBone.Name);
		if (ParentBody < 0 || Asset->BodySetups[ParentBody]->AggGeom.IsEmpty()) continue;

		// 비어 있던 바디에 도형을 복구할 때 기존 관절을 중복 생성하지 않는다.
		for (const FConstraintSetup& Joint : Asset->ConstraintSetups)
		{
			if (Joint.ChildBoneName == Bone.Name ||
				(Joint.ParentBoneName == Bone.Name && Joint.ChildBoneName == ParentBone.Name))
				return true;
		}
		Asset->ConstraintSetups.Add(MakeBoneConstraint(Bone.Name, ParentBone.Name, Capsule.Rotation,
			ChildBind, RigidBindMatrix(ParentBone).Inverse(), Params));
		break;
	}
	return true;
}
