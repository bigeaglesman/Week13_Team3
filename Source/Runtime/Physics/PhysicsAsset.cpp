#include "EnginePCH.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Render/SkeletalMesh.h"

#include "Serialization/MathSerialization.h"
#include "Serialization/StructuredArchiveSlots.h"

namespace
{
	// 파일 형식이 바뀌면 올린다. 다른 버전 파일은 읽지 않는다.
	constexpr int32 PhysicsAssetVersion = 1;

	// 자유도는 사람이 읽을 수 있게 이름으로 저장한다.
	void SerializeMotion(FStructuredArchive::FRecord Record, const char* FieldName, EConstraintMotion& Motion)
	{
		FArchive& Ar = Record.GetUnderlyingArchive();
		static const char* Names[] = { "Free", "Limited", "Locked" };

		FString Name = Ar.IsSaving() ? Names[static_cast<int32>(Motion)] : FString();
		Record << SA_VALUE(FieldName, Name);
		if (Ar.IsLoading())
		{
			for (int32 i = 0; i < 3; ++i)
			{
				if (Name == Names[i])
				{
					Motion = static_cast<EConstraintMotion>(i);
					return;
				}
			}
			Ar.SetError();
		}
	}

	void SerializeConstraintSetup(FStructuredArchive::FRecord Record, FConstraintSetup& Setup)
	{
		Record << SA_VALUE("JointName", Setup.JointName);
		Record << SA_VALUE("ChildBone", Setup.ChildBoneName);
		Record << SA_VALUE("ParentBone", Setup.ParentBoneName);
		Record << SA_VALUE("ChildFrame", Setup.ChildFrame);
		Record << SA_VALUE("ParentFrame", Setup.ParentFrame);

		SerializeMotion(Record, "LinearX", Setup.LinearXMotion);
		SerializeMotion(Record, "LinearY", Setup.LinearYMotion);
		SerializeMotion(Record, "LinearZ", Setup.LinearZMotion);
		Record << SA_VALUE("LinearLimit", Setup.LinearLimit);

		SerializeMotion(Record, "Twist", Setup.TwistMotion);
		SerializeMotion(Record, "Swing1", Setup.Swing1Motion);
		SerializeMotion(Record, "Swing2", Setup.Swing2Motion);
		Record << SA_VALUE("TwistLimitDeg", Setup.TwistLimitDeg);
		Record << SA_VALUE("Swing1LimitDeg", Setup.Swing1LimitDeg);
		Record << SA_VALUE("Swing2LimitDeg", Setup.Swing2LimitDeg);

		Record << SA_VALUE("DisableCollision", Setup.bDisableCollision);
		Record << SA_VALUE("AngularDriveStiffness", Setup.AngularDriveStiffness);
		Record << SA_VALUE("AngularDriveDamping", Setup.AngularDriveDamping);
	}
}

UPhysicsAsset::~UPhysicsAsset()
{
	Clear();

	if (OwningMesh)
	{
		OwningMesh->OnPhysicsAssetDestroyed(this);
		OwningMesh = nullptr;
	}
}

void UPhysicsAsset::Clear()
{
	// UBodySetup 소멸자가 OnBodySetupDestroyed로 이 배열을 건드리므로, 먼저 꺼내 두고 지운다.
	TArray<UBodySetup*> Setups = std::move(BodySetups);
	BodySetups.Empty();
	for (UBodySetup* Setup : Setups)
	{
		delete Setup;
	}
	ConstraintSetups.Empty();
}

void UPhysicsAsset::OnBodySetupDestroyed(UBodySetup* Setup)
{
	for (int32 i = 0; i < BodySetups.Num(); ++i)
	{
		if (BodySetups[i] == Setup)
		{
			BodySetups[i] = nullptr;   // 순서를 바꾸지 않도록 비워만 둔다. 쓰는 쪽은 nullptr을 건너뛴다.
		}
	}
}

int32 UPhysicsAsset::FindBodyIndex(const FString& BoneName) const
{
	for (int32 i = 0; i < BodySetups.Num(); ++i)
	{
		if (BodySetups[i] && BodySetups[i]->BoneName == BoneName)
		{
			return i;
		}
	}
	return -1;
}

bool UPhysicsAsset::HasConstraintBetween(const FString& BoneA, const FString& BoneB) const
{
	for (const FConstraintSetup& Joint : ConstraintSetups)
	{
		if ((Joint.ChildBoneName == BoneA && Joint.ParentBoneName == BoneB)
			|| (Joint.ChildBoneName == BoneB && Joint.ParentBoneName == BoneA))
			return true;
	}
	return false;
}

bool UPhysicsAsset::RemoveBody(const FString& BoneName)
{
	const int32 BodyIndex = FindBodyIndex(BoneName);
	if (BodyIndex < 0) return false;

	for (int32 i = ConstraintSetups.Num() - 1; i >= 0; --i)
	{
		const FConstraintSetup& Joint = ConstraintSetups[i];
		if (Joint.ParentBoneName == BoneName || Joint.ChildBoneName == BoneName)
			ConstraintSetups.RemoveAt(i);
	}

	UBodySetup* Setup = BodySetups[BodyIndex];
	BodySetups.RemoveAt(BodyIndex);
	delete Setup;
	return true;
}

void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	int32 Version = Ar.IsSaving() ? PhysicsAssetVersion : 0;
	Record << SA_VALUE("Version", Version);
	if (Version != PhysicsAssetVersion)
	{
		Ar.SetError();
		return;
	}

	if (Ar.IsLoading())
	{
		Clear();
	}

	// ---- 바디 ----
	TArray<UBodySetup*> ValidSetups;
	if (Ar.IsSaving())
	{
		for (UBodySetup* Setup : BodySetups)
		{
			if (Setup) ValidSetups.Add(Setup);
		}
	}

	int32 NumBodies = Ar.IsSaving() ? ValidSetups.Num() : 0;
	FStructuredArchive::FArray Bodies = Record.EnterArray("Bodies", NumBodies);
	for (int32 i = 0; i < NumBodies; ++i)
	{
		UBodySetup* Setup = Ar.IsSaving() ? ValidSetups[i] : NewObject<UBodySetup>(this);
		Setup->Serialize(Bodies.EnterElement().EnterRecord());
		if (Ar.IsLoading())
		{
			BodySetups.Add(Setup);
		}
	}

	// ---- 관절 ----
	int32 NumConstraints = Ar.IsSaving() ? ConstraintSetups.Num() : 0;
	FStructuredArchive::FArray Constraints = Record.EnterArray("Constraints", NumConstraints);
	if (Ar.IsLoading())
	{
		ConstraintSetups.SetNum(NumConstraints);
	}
	for (int32 i = 0; i < NumConstraints; ++i)
	{
		SerializeConstraintSetup(Constraints.EnterElement().EnterRecord(), ConstraintSetups[i]);
	}
}
