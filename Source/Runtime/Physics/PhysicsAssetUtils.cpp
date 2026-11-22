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
	};

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

	// ---- 1. 뼈마다 담당 정점의 로컬 AABB ----
	// 정점은 가중치가 가장 큰 뼈 하나에만 넣는다 (UE의 EVW_DominantWeight).
	// InverseBindPose는 컴포넌트 공간 → 뼈 로컬 공간 변환이다.
	TArray<FBoneVertexBounds> Bounds;
	Bounds.SetNum(NumBones);

	const int32 NumVertices = std::min(LOD.BindVertices.Num(), LOD.SkinWeights.Num());
	for (int32 v = 0; v < NumVertices; ++v)
	{
		const FSkinWeight& Weight = LOD.SkinWeights[v];
		int32 Dominant = -1;
		float BestWeight = 0.0f;
		for (int32 k = 0; k < MaxBoneInfluences; ++k)
		{
			if (Weight.Weight[k] > BestWeight)
			{
				BestWeight = Weight.Weight[k];
				Dominant = Weight.BoneIndex[k];
			}
		}
		if (Dominant < 0 || Dominant >= NumBones)
		{
			continue;
		}

		const FVector P = Bones[Dominant].InverseBindPose.TransformPosition(LOD.BindVertices[v].Position);
		FBoneVertexBounds& B = Bounds[Dominant];
		B.Min = FVector(std::min(B.Min.X, P.X), std::min(B.Min.Y, P.Y), std::min(B.Min.Z, P.Z));
		B.Max = FVector(std::max(B.Max.X, P.X), std::max(B.Max.Y, P.Y), std::max(B.Max.Z, P.Z));
		++B.NumVertices;
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
		const FVector Center = (B.Max + B.Min) * 0.5f;
		const int32 Long = GetLongestAxis(Extent);
		if (GetAxis(Extent, Long) * 2.0f < MinBoneSize)
		{
			continue;
		}

		// 반지름 = 나머지 두 축 중 큰 쪽, 반높이 = 긴 축 (반구 포함)
		const float Radius = std::max(GetAxis(Extent, (Long + 1) % 3), GetAxis(Extent, (Long + 2) % 3));
		const float HalfHeight = std::max(GetAxis(Extent, Long), Radius);

		FKSphylElem Elem;
		Elem.Center = Center;
		Elem.Rotation = RotateZToAxis(Long);
		Elem.Radius = std::max(Radius, MinBoneSize * 0.25f);
		Elem.Length = 2.0f * std::max(HalfHeight - Elem.Radius, 0.0f);

		UBodySetup* Setup = NewObject<UBodySetup>(Asset);
		Setup->BoneName = Bones[i].Name;
		Setup->AggGeom.SphylElems.Add(Elem);
		Setup->DefaultInstance.bSimulatePhysics = true;   // 래그돌 바디의 기본값

		BoneToBody[i] = Asset->BodySetups.Num();
		Asset->BodySetups.Add(Setup);
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
		const FMatrix ChildBindComponent = Bones[i].InverseBindPose.Inverse();
		const FMatrix FrameInParent = Constraint.ChildFrame.GetLocalMatrix() * ChildBindComponent * Bones[Parent].InverseBindPose;
		Constraint.ParentFrame = ToFTransform(ToPxNoScale(FrameInParent));

		Constraint.TwistLimitDeg = Params.TwistLimitDeg;
		Constraint.Swing1LimitDeg = Params.Swing1LimitDeg;
		Constraint.Swing2LimitDeg = Params.Swing2LimitDeg;

		Asset->ConstraintSetups.Add(Constraint);
	}

	return Asset;
}
