#include "EnginePCH.h"
#include "BoneHierarchyPanel.h"
#include "Containers/Set.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "Runtime/Render/SkeletalMeshData.h"

FBoneHierarchyPanel::FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Bone Hierarchy")
	, ColliderWindowName(MakeWindowName("Collider Hierarchy"))
{
}

const char* FBoneHierarchyPanel::GetPanelName() const
{
	return Editor.IsPhysicsAssetMode() ? "Collider Hierarchy" : FSkeletalMeshEditorPanel::GetPanelName();
}

const char* FBoneHierarchyPanel::GetWindowName() const
{
	return Editor.IsPhysicsAssetMode() ? ColliderWindowName.c_str() : FSkeletalMeshEditorPanel::GetWindowName();
}

void FBoneHierarchyPanel::DrawContent()
{
	if (Editor.IsPhysicsAssetMode())
	{
		DrawColliderTree();
		return;
	}
	DrawTree(Editor);
}

void FBoneHierarchyPanel::DrawColliderTree()
{
	const USkeletalMesh* Mesh = Editor.GetMesh();
	const UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	TSet<FString> BodyBones;
	if (Asset)
	{
		for (const UBodySetup* Setup : Asset->BodySetups)
		{
			if (Setup && !Setup->AggGeom.IsEmpty())
			{
				BodyBones.Add(Setup->BoneName);
			}
		}
	}

	// 부모가 자식보다 먼저 온다. 도형이 없는 본은 가장 가까운 상위 바디를 자식에게 전한다.
	TArray<TArray<int32>> Children;
	Children.SetNum(Bones.Num());
	TArray<int32> NearestBody;
	NearestBody.Reserve(Bones.Num());
	TArray<int32> Roots;
	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		const FBoneInfo& Bone = Bones[BoneIndex];
		const int32 Parent = Bone.ParentIndex;
		const int32 ParentBody = Parent >= 0 ? NearestBody[Parent] : -1;
		if (!BodyBones.Contains(Bone.Name))
		{
			NearestBody.Add(ParentBody);
			continue;
		}
		NearestBody.Add(BoneIndex);
		if (ParentBody >= 0)
		{
			Children[ParentBody].Add(BoneIndex);
		}
		else
		{
			Roots.Add(BoneIndex);
		}
	}

	if (Roots.IsEmpty())
	{
		ImGui::TextDisabled("No colliders on this mesh.");
		return;
	}
	if (ImGui::BeginChild("ColliderHierarchyChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		for (int32 Root : Roots)
		{
			DrawBoneHierarchy(Editor, Root, &Children);
		}
	}
	ImGui::EndChild();
}

void FBoneHierarchyPanel::DrawTree(FSkeletalPreviewEditor& InEditor)
{
	if (!InEditor.GetMesh())
	{
		return;
	}

	if (ImGui::BeginChild("BoneHierarchyChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);

		for (int32 RootBoneIndex : InEditor.GetRootBoneIndices())
		{
			DrawBoneHierarchy(InEditor, RootBoneIndex);
		}

		ImGui::PopStyleVar();
	}

	ImGui::EndChild();
}

void FBoneHierarchyPanel::DrawBoneHierarchy(FSkeletalPreviewEditor& InEditor, int32 BoneIndex,
	const TArray<TArray<int32>>* ColliderChildren)
{
	ImGuiTreeNodeFlags Flag =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	const TArray<FEditorBoneItem>& BoneItems = InEditor.GetBoneItems();

	const TArray<int32>& Children = ColliderChildren ? (*ColliderChildren)[BoneIndex] : BoneItems[BoneIndex].ChildBoneIndices;
	if (Children.IsEmpty())
	{
		Flag |= ImGuiTreeNodeFlags_Leaf;
	}

	if (InEditor.GetSelectedBoneIndex() == BoneIndex)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::PushID(BoneIndex);

	// 표시 이름과 ID를 분리하여 축약된 이름이 겹쳐도 구분되게 함
	const FString& BoneName = BoneItems[BoneIndex].Name;

	// 접힌 노드도 선택할 수 있도록 펼침 상태와 선택 처리를 분리
	const bool bOpen = ImGui::TreeNodeEx("Bone", Flag, "%s", BoneName.c_str());

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		InEditor.SelectBone(BoneIndex);
	}

	if (bOpen)
	{
		for (int32 ChildIndex : Children)
		{
			DrawBoneHierarchy(InEditor, ChildIndex, ColliderChildren);
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}
