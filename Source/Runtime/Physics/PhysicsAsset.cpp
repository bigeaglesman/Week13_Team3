#include "EnginePCH.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <limits>

namespace
{
	// 인체·물 기준 밀도 (kg/m^3). 언리얼 물리 바디의 부피×밀도 질량 계산 방식과 같다.
	constexpr float DefaultBodyDensity = 1000.0f;

	bool HasValidBounds(const FBox& Box)
	{
		return Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
	}

	// 컴포넌트 공간 AABB의 8개 코너를 InverseBindPose로 본 로컬 공간에 투영해 로컬 AABB를 구한다.
	FBox TransformBoxToBoneLocal(const FBox& CompBox, const FMatrix& InverseBindPose)
	{
		FBox LocalBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
		const FVector Corners[8] = {
			FVector(CompBox.Min.X, CompBox.Min.Y, CompBox.Min.Z),
			FVector(CompBox.Max.X, CompBox.Min.Y, CompBox.Min.Z),
			FVector(CompBox.Min.X, CompBox.Max.Y, CompBox.Min.Z),
			FVector(CompBox.Max.X, CompBox.Max.Y, CompBox.Min.Z),
			FVector(CompBox.Min.X, CompBox.Min.Y, CompBox.Max.Z),
			FVector(CompBox.Max.X, CompBox.Min.Y, CompBox.Max.Z),
			FVector(CompBox.Min.X, CompBox.Max.Y, CompBox.Max.Z),
			FVector(CompBox.Max.X, CompBox.Max.Y, CompBox.Max.Z),
		};

		for (const FVector& C : Corners)
		{
			const FVector P = InverseBindPose.TransformPosition(C);
			LocalBox.Min.X = std::min(LocalBox.Min.X, P.X);
			LocalBox.Min.Y = std::min(LocalBox.Min.Y, P.Y);
			LocalBox.Min.Z = std::min(LocalBox.Min.Z, P.Z);
			LocalBox.Max.X = std::max(LocalBox.Max.X, P.X);
			LocalBox.Max.Y = std::max(LocalBox.Max.Y, P.Y);
			LocalBox.Max.Z = std::max(LocalBox.Max.Z, P.Z);
		}
		return LocalBox;
	}

	// 엔진 기본 캡슐 축(+Z)을 원하는 본 방향(TargetDir)으로 돌리는 쿼터니언을 계산한다.
	FQuat MakeQuatFromZToDir(const FVector& TargetDir)
	{
		const FVector Dir = TargetDir.GetSafeNormal();
		if (Dir.SizeSquared() <= SMALL_NUMBER)
		{
			return FQuat::Identity();
		}

		const float Dot = std::clamp(Dir.Z, -1.0f, 1.0f);
		if (Dot > 0.9999f)
		{
			return FQuat::Identity();
		}
		if (Dot < -0.9999f)
		{
			return FQuat::MakeFromAxisAngle(FVector(1.0f, 0.0f, 0.0f), PI);
		}

		const FVector Axis = FVector(0.0f, 0.0f, 1.0f).Cross(Dir).GetSafeNormal();
		return FQuat::MakeFromAxisAngle(Axis, std::acos(Dot));
	}

	// 도형의 기하 부피(m^3) × 밀도(kg/m^3)로 질량을 자동 계산한다.
	// 솔버 안정성을 위해 질량비가 과도해지지 않도록 [0.5, 20.0] kg 범위로 클램프한다.
	float ComputeMassFromShape(const FBodyShapeDesc& Shape, float Density = DefaultBodyDensity)
	{
		float Volume = 0.0f;
		switch (Shape.Type)
		{
		case EBodyShapeType::Sphere:
		{
			const float R = std::max(Shape.Radius, 0.01f);
			Volume = (4.0f / 3.0f) * PI * R * R * R;
			break;
		}
		case EBodyShapeType::Capsule:
		{
			const float R = std::max(Shape.Radius, 0.01f);
			const float CylHeight = 2.0f * std::max(Shape.HalfHeight - R, 0.0f);
			Volume = (4.0f / 3.0f) * PI * R * R * R + PI * R * R * CylHeight;
			break;
		}
		case EBodyShapeType::Box:
		{
			Volume = 8.0f * std::max(Shape.HalfExtent.X, 0.01f)
				* std::max(Shape.HalfExtent.Y, 0.01f)
				* std::max(Shape.HalfExtent.Z, 0.01f);
			break;
		}
		default:
			return 1.0f;
		}

		return std::clamp(Volume * Density, 0.5f, 20.0f);
	}

	void SerializeVector(FStructuredArchive::FRecord ParentRecord, const char* FieldName, FVector& Vec)
	{
		auto VecRecord = ParentRecord.EnterRecord(FieldName);
		VecRecord << SA_VALUE("X", Vec.X);
		VecRecord << SA_VALUE("Y", Vec.Y);
		VecRecord << SA_VALUE("Z", Vec.Z);
	}

	void SerializeQuat(FStructuredArchive::FRecord ParentRecord, const char* FieldName, FQuat& Quat)
	{
		auto QuatRecord = ParentRecord.EnterRecord(FieldName);
		QuatRecord << SA_VALUE("X", Quat.X);
		QuatRecord << SA_VALUE("Y", Quat.Y);
		QuatRecord << SA_VALUE("Z", Quat.Z);
		QuatRecord << SA_VALUE("W", Quat.W);
	}

	void SerializeBodySetup(FStructuredArchive::FRecord Record, FSkeletalBodySetup& Body)
	{
		Record << SA_VALUE("BoneName", Body.BoneName);

		uint8 ShapeType = static_cast<uint8>(Body.Shape.Type);
		Record << SA_VALUE("ShapeType", ShapeType);
		Body.Shape.Type = static_cast<EBodyShapeType>(ShapeType);

		SerializeVector(Record, "HalfExtent", Body.Shape.HalfExtent);
		Record << SA_VALUE("Radius", Body.Shape.Radius);
		Record << SA_VALUE("HalfHeight", Body.Shape.HalfHeight);
		SerializeVector(Record, "LocalOffset", Body.Shape.LocalOffset);
		SerializeQuat(Record, "LocalRotation", Body.Shape.LocalRotation);

		Record << SA_VALUE("MassInKg", Body.MassInKg);
		Record << SA_VALUE("LinearDamping", Body.LinearDamping);
		Record << SA_VALUE("AngularDamping", Body.AngularDamping);
	}

	void SerializeConstraintSetup(FStructuredArchive::FRecord Record, FPhysicsConstraintSetup& Constraint)
	{
		Record << SA_VALUE("JointName", Constraint.JointName);
		Record << SA_VALUE("ConstraintBone1", Constraint.ConstraintBone1);
		Record << SA_VALUE("ConstraintBone2", Constraint.ConstraintBone2);

		uint8 Swing1 = static_cast<uint8>(Constraint.Swing1Motion);
		uint8 Swing2 = static_cast<uint8>(Constraint.Swing2Motion);
		uint8 Twist = static_cast<uint8>(Constraint.TwistMotion);
		Record << SA_VALUE("Swing1Motion", Swing1);
		Record << SA_VALUE("Swing2Motion", Swing2);
		Record << SA_VALUE("TwistMotion", Twist);
		Constraint.Swing1Motion = static_cast<EAngularConstraintMotion>(Swing1);
		Constraint.Swing2Motion = static_cast<EAngularConstraintMotion>(Swing2);
		Constraint.TwistMotion = static_cast<EAngularConstraintMotion>(Twist);

		Record << SA_VALUE("Swing1LimitDegrees", Constraint.Swing1LimitDegrees);
		Record << SA_VALUE("Swing2LimitDegrees", Constraint.Swing2LimitDegrees);
		Record << SA_VALUE("TwistLimitDegrees", Constraint.TwistLimitDegrees);
		Record << SA_VALUE("AngularDamping", Constraint.AngularDamping);
		Record << SA_VALUE("AngularStiffness", Constraint.AngularStiffness);
		Record << SA_VALUE("bDisableCollision", Constraint.bDisableCollision);
	}

	void SerializePhysicsAssetRecord(FStructuredArchive::FRecord Record, UPhysicsAsset& Asset)
	{
		FArchive& Ar = Record.GetUnderlyingArchive();

		int32 Version = Ar.IsSaving() ? 1 : 0;
		Record << SA_VALUE("Version", Version);
		if (Version != 1)
		{
			Ar.SetError();
			return;
		}

		int32 BodyCount = Ar.IsSaving() ? Asset.SkeletalBodySetups.Num() : 0;
		auto BodiesArray = Record.EnterArray("Bodies", BodyCount);
		if (Ar.IsLoading())
		{
			Asset.SkeletalBodySetups.SetNum(BodyCount);
		}
		for (int32 i = 0; i < BodyCount; ++i)
		{
			SerializeBodySetup(BodiesArray.EnterElement().EnterRecord(), Asset.SkeletalBodySetups[i]);
		}

		int32 ConstraintCount = Ar.IsSaving() ? Asset.ConstraintSetups.Num() : 0;
		auto ConstraintsArray = Record.EnterArray("Constraints", ConstraintCount);
		if (Ar.IsLoading())
		{
			Asset.ConstraintSetups.SetNum(ConstraintCount);
		}
		for (int32 i = 0; i < ConstraintCount; ++i)
		{
			SerializeConstraintSetup(ConstraintsArray.EnterElement().EnterRecord(), Asset.ConstraintSetups[i]);
		}
	}
}

int32 UPhysicsAsset::FindBodyIndex(const FString& BoneName) const
{
	for (int32 i = 0; i < SkeletalBodySetups.Num(); ++i)
	{
		if (SkeletalBodySetups[i].BoneName == BoneName)
		{
			return i;
		}
	}
	return -1;
}

int32 UPhysicsAsset::FindConstraintIndex(const FString& Bone1Name, const FString& Bone2Name) const
{
	for (int32 i = 0; i < ConstraintSetups.Num(); ++i)
	{
		const FPhysicsConstraintSetup& C = ConstraintSetups[i];
		if ((C.ConstraintBone1 == Bone1Name && C.ConstraintBone2 == Bone2Name) ||
			(C.ConstraintBone1 == Bone2Name && C.ConstraintBone2 == Bone1Name))
		{
			return i;
		}
	}
	return -1;
}

void UPhysicsAsset::Clear()
{
	SkeletalBodySetups.Reset();
	ConstraintSetups.Reset();
}

void UPhysicsAsset::GenerateFromSkeletalMesh(const USkeletalMesh* Mesh, float MinBoneSize)
{
	Clear();

	if (!Mesh)
	{
		return;
	}

	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
	{
		return;
	}

	// 스키닝 정점 바운드(LOD 0)가 있으면 정점이 붙지 않은 보조 본(Armature, root, 리프 등)을 자동 제외한다.
	const TArray<FBox>* BoneBounds = nullptr;
	if (!Mesh->GetRenderData().LODRenderData.IsEmpty())
	{
		const TArray<FBox>& LODBounds = Mesh->GetRenderData().LODRenderData[0].BoneBounds;
		if (LODBounds.Num() == NumBones)
		{
			BoneBounds = &LODBounds;
		}
	}

	auto IsSkinnedBone = [&](int32 Index) -> bool
	{
		return !BoneBounds || HasValidBounds((*BoneBounds)[Index]);
	};

	// 각 본의 직접 자식 목록을 수집한다.
	TArray<TArray<int32>> Children;
	Children.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 ParentIdx = Bones[i].ParentIndex;
		if (ParentIdx >= 0 && ParentIdx < NumBones)
		{
			Children[ParentIdx].Add(i);
		}
	}

	const float ClampedMinSize = std::max(MinBoneSize, 0.01f);

	// 1) 스키닝된 본 중 MinBoneSize 이상인 본에만 Capsule 또는 Sphere 바디를 생성한다.
	for (int32 i = 0; i < NumBones; ++i)
	{
		if (!IsSkinnedBone(i))
		{
			continue;
		}

		// 스키닝된 자식 중 가장 긴 본 벡터를 대표 방향으로 삼는다 (분기 본에서도 주 축을 안정적으로 선택).
		int32 PrimaryChildIdx = -1;
		float BestChildLen = 0.0f;
		for (int32 ChildIdx : Children[i])
		{
			if (!IsSkinnedBone(ChildIdx))
			{
				continue;
			}
			const float Len = Bones[ChildIdx].LocalBindPose.Location.Length();
			if (Len > BestChildLen)
			{
				BestChildLen = Len;
				PrimaryChildIdx = ChildIdx;
			}
		}

		FSkeletalBodySetup BodySetup;
		BodySetup.BoneName = Bones[i].Name;
		BodySetup.LinearDamping = 0.2f;
		BodySetup.AngularDamping = 1.5f;

		if (PrimaryChildIdx >= 0 && BestChildLen >= ClampedMinSize)
		{
			const FVector ChildOffset = Bones[PrimaryChildIdx].LocalBindPose.Location;
			const float Radius = std::clamp(BestChildLen * 0.28f, 0.03f, 0.13f);
			const float HalfHeight = std::max(BestChildLen * 0.5f, Radius);

			BodySetup.Shape.Type = EBodyShapeType::Capsule;
			BodySetup.Shape.Radius = Radius;
			BodySetup.Shape.HalfHeight = HalfHeight;
			BodySetup.Shape.LocalOffset = ChildOffset * 0.5f;
			BodySetup.Shape.LocalRotation = MakeQuatFromZToDir(ChildOffset);
		}
		else if (BoneBounds && PrimaryChildIdx < 0)
		{
			// 스키닝된 자식이 없는 말단 본(머리·손·발 등): 본 로컬 정점 바운드로 크기를 측정해 구를 생성한다.
			const FBox LocalBounds = TransformBoxToBoneLocal((*BoneBounds)[i], Bones[i].InverseBindPose);
			const FVector Extent = (LocalBounds.Max - LocalBounds.Min) * 0.5f;
			const float MaxDiameter = 2.0f * std::max({ Extent.X, Extent.Y, Extent.Z });

			// 손가락 끝·발가락 등 너무 작은 말단 본은 MinBoneSize 기준으로 제외한다.
			if (MaxDiameter < ClampedMinSize * 1.5f)
			{
				continue;
			}

			BodySetup.Shape.Type = EBodyShapeType::Sphere;
			BodySetup.Shape.Radius = std::clamp(MaxDiameter * 0.35f, 0.035f, 0.14f);
			BodySetup.Shape.LocalOffset = (LocalBounds.Min + LocalBounds.Max) * 0.5f;
			BodySetup.Shape.LocalRotation = FQuat::Identity();
		}
		else
		{
			// 자식은 있으나 본 길이가 MinBoneSize 미만(손가락 마디 등)이면 바디를 만들지 않는다.
			continue;
		}

		// 도형 부피 × 밀도로 질량을 자동 계산한다.
		BodySetup.MassInKg = ComputeMassFromShape(BodySetup.Shape);
		SkeletalBodySetups.Add(BodySetup);
	}

	// 2) 생성된 각 바디를 가장 가까운 조상 바디와 일괄 기본 제한 각도(45°/45°/30°) 조인트로 연결한다.
	//    관절별 세부 튜닝은 JSON 에셋 파일(SaveToFile/LoadFromFile)에서 데이터로 편집한다.
	for (const FSkeletalBodySetup& BodySetup : SkeletalBodySetups)
	{
		const int32 BoneIdx = Mesh->FindBoneIndex(BodySetup.BoneName);
		if (BoneIdx < 0)
		{
			continue;
		}

		int32 AncestorIdx = Bones[BoneIdx].ParentIndex;
		while (AncestorIdx >= 0 && AncestorIdx < NumBones)
		{
			if (FindBodyIndex(Bones[AncestorIdx].Name) >= 0)
			{
				FPhysicsConstraintSetup Constraint;
				Constraint.JointName = BodySetup.BoneName;
				Constraint.ConstraintBone1 = BodySetup.BoneName;      // 자식 본
				Constraint.ConstraintBone2 = Bones[AncestorIdx].Name; // 부모 본
				ConstraintSetups.Add(Constraint);
				break;
			}
			AncestorIdx = Bones[AncestorIdx].ParentIndex;
		}
	}
}

bool UPhysicsAsset::SaveToFile(const FString& FilePath, FString& OutError) const
{
	OutError.clear();

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		SerializePhysicsAssetRecord(Archive.Open().EnterRecord(), const_cast<UPhysicsAsset&>(*this));
	}

	if (Writer.IsError())
	{
		OutError = "PhysicsAsset: failed to serialize JSON";
		return false;
	}

	std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		OutError = "PhysicsAsset: cannot open output file";
		return false;
	}

	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (!File)
	{
		OutError = "PhysicsAsset: failed to write file";
		return false;
	}

	return true;
}

bool UPhysicsAsset::LoadFromFile(const FString& FilePath, FString& OutError)
{
	OutError.clear();

	std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "PhysicsAsset: cannot open file or invalid size";
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "PhysicsAsset: cannot read file";
		return false;
	}

	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: invalid JSON";
		return false;
	}

	Clear();
	FStructuredArchive Archive(Formatter);
	SerializePhysicsAssetRecord(Archive.Open().EnterRecord(), *this);

	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: unsupported version or corrupt data";
		Clear();
		return false;
	}

	return true;
}
