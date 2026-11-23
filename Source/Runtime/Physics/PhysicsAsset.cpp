#include "EnginePCH.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Render/SkeletalMesh.h"
#include <cmath>

namespace
{
	constexpr float MinBoneSizeRatio = 0.06f;   // 메시 크기 대비 정점 범위가 이보다 작은 본(손가락 등)은 정점을 부모 본에 넘긴다

	// 로컬 Z축을 Dir로 돌리는 회전
	FQuat MakeZToDirection(const FVector& Dir)
	{
		const FVector Z(0.0f, 0.0f, 1.0f);
		const float Cos = Z.Dot(Dir);
		if (Cos > 0.9999f) return FQuat::Identity();
		if (Cos < -0.9999f) return FQuat::MakeFromAxisAngle(FVector(1.0f, 0.0f, 0.0f), PI);

		const FVector Axis = Z.Cross(Dir).GetSafeNormal();
		const float Angle = std::acos(Cos);
		// 회전 방향 규약(왼손/오른손)에 기대지 않고 결과로 부호를 고른다
		const FQuat Q = FQuat::MakeFromAxisAngle(Axis, Angle);
		return Q.RotateVector(Z).Dot(Dir) > 0.99f ? Q : FQuat::MakeFromAxisAngle(Axis, -Angle);
	}

	FBox MakeEmptyBox()
	{
		return FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	}

	void AddToBox(FBox& Box, const FVector& P)
	{
		Box.Min = FVector(std::fmin(Box.Min.X, P.X), std::fmin(Box.Min.Y, P.Y), std::fmin(Box.Min.Z, P.Z));
		Box.Max = FVector(std::fmax(Box.Max.X, P.X), std::fmax(Box.Max.Y, P.Y), std::fmax(Box.Max.Z, P.Z));
	}

	// 상자의 가장 긴 축을 캡슐 축으로 삼고 나머지 두 축 중 큰 쪽을 반지름으로 삼는다
	FKSphylElem MakeSphylFromBox(const FBox& Box)
	{
		const FVector Center = (Box.Min + Box.Max) * 0.5f;
		const FVector Half = (Box.Max - Box.Min) * 0.5f;

		FVector Axis(0.0f, 0.0f, 1.0f);
		float HalfMajor = Half.Z;
		float Radius = std::fmax(Half.X, Half.Y);
		if (Half.X >= Half.Y && Half.X >= Half.Z)
		{
			Axis = FVector(1.0f, 0.0f, 0.0f);
			HalfMajor = Half.X;
			Radius = std::fmax(Half.Y, Half.Z);
		}
		else if (Half.Y >= Half.Z)
		{
			Axis = FVector(0.0f, 1.0f, 0.0f);
			HalfMajor = Half.Y;
			Radius = std::fmax(Half.X, Half.Z);
		}

		FKSphylElem Sphyl;
		Sphyl.Center = Center;
		Sphyl.Rotation = MakeZToDirection(Axis).ToFRotator();
		Sphyl.Radius = Radius;
		Sphyl.Length = std::fmax(2.0f * (HalfMajor - Radius), 0.0f);   // 반구를 포함한 전체 길이가 상자 긴 축과 같다
		return Sphyl;
	}
}

UPhysicsAsset::~UPhysicsAsset()
{
	const TArray<UBodySetup*> Setups = SkeletalBodySetups;
	SkeletalBodySetups.Empty();
	for (UBodySetup* Setup : Setups)
		delete Setup;

	// UObject 일괄 정리에서 메시보다 먼저 해제되면 메시가 다시 delete하지 않게 비운다.
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(GetOuter()))
		if (Mesh->PhysicsAsset == this)
			Mesh->PhysicsAsset = nullptr;
}

void UPhysicsAsset::CreateFromSkeletalMesh(const USkeletalMesh& Mesh)
{
	const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
	const TArray<FSkeletalMeshLODRenderData>& LODs = Mesh.GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const int32 NumBones = Bones.Num();

	// UE EVW_DominantWeight. 정점은 가중치가 가장 큰 본 하나에 속한다
	TArray<TArray<int32>> BoneVertices;
	BoneVertices.SetNum(NumBones);
	for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
	{
		const FSkinWeight& Weight = LOD.SkinWeights[V];
		int32 Dominant = 0;
		for (int32 K = 1; K < MaxBoneInfluences; ++K)
			if (Weight.Weight[K] > Weight.Weight[Dominant])
				Dominant = K;
		if (Weight.Weight[Dominant] > 0.0f && Weight.BoneIndex[Dominant] < NumBones)
			BoneVertices[Weight.BoneIndex[Dominant]].Add(V);
	}

	// UE bWalkPastSmall. 자식이 부모보다 뒤 인덱스라 뒤에서부터 넘기면 손가락 끝 → 마디 → 손 순서로 모인다
	const FVector MeshExtent = LOD.BindAABB.Max - LOD.BindAABB.Min;
	const float MinBoneSize = std::fmax(MeshExtent.X, std::fmax(MeshExtent.Y, MeshExtent.Z)) * MinBoneSizeRatio;
	for (int32 i = NumBones - 1; i > 0; --i)
	{
		const int32 Parent = Bones[i].ParentIndex;
		if (Parent < 0 || BoneVertices[i].IsEmpty())
			continue;

		FBox Box = MakeEmptyBox();
		for (int32 V : BoneVertices[i])
			AddToBox(Box, LOD.BindVertices[V].Position);
		const FVector Extent = Box.Max - Box.Min;
		if (std::fmax(Extent.X, std::fmax(Extent.Y, Extent.Z)) >= MinBoneSize)
			continue;

		for (int32 V : BoneVertices[i])
			BoneVertices[Parent].Add(V);
		BoneVertices[i].Empty();
	}

	for (int32 i = 0; i < NumBones; ++i)
	{
		if (BoneVertices[i].IsEmpty())
			continue;

		// 스키닝과 같은 역바인드 행렬로 정점을 본 로컬 공간(스케일 적용 전)으로 옮겨 감싼다
		FBox Box = MakeEmptyBox();
		for (int32 V : BoneVertices[i])
			AddToBox(Box, Bones[i].InverseBindPose.TransformPosition(LOD.BindVertices[V].Position));

		UBodySetup* Setup = NewObject<UBodySetup>(this);
		Setup->BoneName = FName(Bones[i].Name);
		Setup->AggGeom.SphylElems.Add(MakeSphylFromBox(Box));
		SkeletalBodySetups.Add(Setup);
	}
}

int32 UPhysicsAsset::FindBodyIndex(const FName& BoneName) const
{
	for (int32 i = 0; i < SkeletalBodySetups.Num(); ++i)
		if (SkeletalBodySetups[i]->BoneName == BoneName)
			return i;
	return -1;
}

void UPhysicsAsset::OnBodySetupDestroyed(UBodySetup* BodySetup)
{
	const int32 Index = SkeletalBodySetups.Find(BodySetup);
	if (Index >= 0)
		SkeletalBodySetups.RemoveAt(static_cast<uint32>(Index));
}
