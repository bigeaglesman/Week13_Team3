#include "EnginePCH.h"
#include "ClothComponent.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"
#include "Physics/PhysXConversion.h"
#include "UObject/UObjectGlobals.h"

#include <unordered_map>

UClothComponent::UClothComponent()
{
	// 메시 전체가 천이다 (섹션 0)
	ClothSectionIndex = 0;

	// 부모 캐릭터가 이번 프레임 뼈를 다 갱신한 뒤에 따라가야 어깨가 한 프레임 늦지 않는다
	PrimaryComponentTick.TickGroup = ETickingGroup::PostUpdateWork;
}

void UClothComponent::SetClothMesh(UStaticMesh* InClothMesh)
{
	ClothMesh = InClothMesh;
	RebuildClothSkeletalMesh();
}

void UClothComponent::OnRegister()
{
	RebuildClothSkeletalMesh();
	Super::OnRegister();
}

void UClothComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	// 만든 스켈레탈 메시는 에셋 경로가 없어 저장되지 않는다. 불러올 때 ClothMesh로 다시 만든다
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		BuiltFromMesh = nullptr;
		RebuildClothSkeletalMesh();
	}
}

void UClothComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "ClothMesh")
	{
		RebuildClothSkeletalMesh();
	}
}

void UClothComponent::TickComponent(float DeltaTime)
{
	// 디테일 패널에서 메시를 바꿨으면 다시 만든다
	if (ClothMesh != BuiltFromMesh)
	{
		RebuildClothSkeletalMesh();
	}

	// 뼈 자리로 먼저 옮긴 다음 천을 돌린다. 옮긴 만큼이 관성이 된다
	FollowAttachBone();
	Super::TickComponent(DeltaTime);
}

void UClothComponent::RebuildClothSkeletalMesh()
{
	// 같은 원본이면 다시 만들지 않는다 (실패한 원본도 다시 시도하지 않는다. 불러오기는 Serialize가 BuiltFromMesh를 비운다)
	if (ClothMesh == BuiltFromMesh)
	{
		return;
	}
	BuiltFromMesh = ClothMesh;

	// 같은 스태틱 메시로 만든 것은 컴포넌트끼리 공유한다 (천 에셋도 그 메시에 붙으므로 한 번만 만들어진다)
	static std::unordered_map<UStaticMesh*, USkeletalMesh*> Cache;
	USkeletalMesh* SkeletalMesh = nullptr;
	if (ClothMesh)
	{
		auto Found = Cache.find(ClothMesh);
		if (Found != Cache.end())
		{
			SkeletalMesh = Found->second;
		}
		else
		{
			SkeletalMesh = CreateSkeletalMeshFromStaticMesh(ClothMesh);
			if (SkeletalMesh)
			{
				Cache.emplace(ClothMesh, SkeletalMesh);
			}
		}
	}

	SetSkeletalMesh(SkeletalMesh);
}

USkeletalMesh* UClothComponent::CreateSkeletalMeshFromStaticMesh(UStaticMesh* StaticMesh)
{
	const FStaticMeshData& Source = StaticMesh->GetMeshData();
	if (Source.Vertices.IsEmpty() || Source.Indices.Num() < 3)
	{
		HTR_LOG(Warning, "[Cloth] ClothMesh '{}' has no vertices.", StaticMesh->GetPath());
		return nullptr;
	}

	FSkeletalMeshData Data;
	Data.SourcePath = StaticMesh->GetPath() + "#Cloth";

	// 뼈는 root 하나. 바인드 포즈가 원점이라 역바인드 행렬은 단위 행렬이다
	FBoneInfo Root;
	Root.Name = "root";
	Root.ParentIndex = -1;
	Root.InverseBindPose = FMatrix::Identity;
	Data.Bones.Add(Root);

	// 정점은 그대로, 가중치는 모두 root에 1
	Data.BindVertices = Source.Vertices;
	FSkinWeight Weight;
	Weight.BoneIndex[0] = 0;
	Weight.Weight[0] = 1.0f;
	Data.SkinWeight.Init(Weight, Source.Vertices.Num());

	// 섹션은 하나로 합친다 (천 = 섹션 0). 재질은 첫 슬롯을 쓴다
	Data.Indices = Source.Indices;
	FSkeletalMeshSection Section;
	Section.StartIndex = 0;
	Section.IndexCount = static_cast<uint32>(Data.Indices.Num());
	Section.MaterialSlotIndex = 0;
	Data.Sections.Add(Section);

	for (const FStaticMaterialSlot& Slot : Source.MaterialSlots)
	{
		FSkeletalMaterialSlot Out;
		Out.Name = Slot.Name;
		Out.BaseColor = Slot.BaseColor;
		Out.DiffuseTexturePath = Slot.DiffuseTexturePath;
		Out.NormalTexturePath = Slot.NormalTexturePath;
		Data.MaterialSlots.Add(Out);
	}
	Data.BindAABB = Source.AABB;

	USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
	FString Error;
	if (!Mesh->Build(std::move(Data), Error))
	{
		HTR_LOG(Warning, "[Cloth] Could not build cloth mesh from '{}': {}", StaticMesh->GetPath(), Error);
		delete Mesh;
		return nullptr;
	}

	HTR_LOG(Info, "[Cloth] Cloth mesh built from '{}' ({} verts)", StaticMesh->GetPath(), Source.Vertices.Num());
	return Mesh;
}

void UClothComponent::FollowAttachBone()
{
	USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(GetAttachParent());
	if (!Parent || AttachBoneName.empty())
	{
		return;
	}

	const int32 BoneIndex = Parent->FindBoneIndex(AttachBoneName);
	const TArray<FMatrix>& BoneTransforms = Parent->GetComponentSpaceTransform();
	if (BoneIndex < 0 || BoneIndex >= BoneTransforms.Num())
	{
		return;
	}

	// 부모 컴포넌트 공간의 뼈 자리(스케일 제외) × 오프셋 = 이 컴포넌트의 상대 트랜스폼.
	// 부모의 월드 트랜스폼(스케일 포함)은 GetWorldMatrix가 위에 곱해 준다.
	const physx::PxTransform Bone = ToPxNoScale(BoneTransforms[BoneIndex]);
	const FQuat BoneRotation = ToFQuat(Bone.q);
	const FVector BoneLocation = ToFVector(Bone.p);

	FTransform Relative = GetTransform();
	Relative.Location = BoneLocation + BoneRotation.RotateVector(AttachLocationOffset);
	Relative.Rotation = BoneRotation * AttachRotationOffset.Quaternion();
	SetTransform(Relative);
}
