#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Physics/BodySetup.h"
#include "Render/SkeletalMesh.h"

FSkeletalMeshDetailsPanel::FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Details")
{
}

void FSkeletalMeshDetailsPanel::DrawContent()
{
	if (Editor.IsPhysicsAssetMode())
	{
		DrawPhysicsAssetDetails();
		return;
	}

	const int32 BoneIndex = Editor.GetSelectedBoneIndex();

	if (ImGui::Button("Reset Transform"))
	{
		Editor.ResetBone(BoneIndex);
	}

	ImGui::Separator();

	FTransform Transform;
	if (!Editor.GetSelectedBoneLocalTransform(Transform))
	{
		ImGui::TextDisabled("Select a bone in Bone Hierarchy.");
		return;
	}



	// 선택이 바뀌면 이전 본의 편집 상태가 새 본에 이어지지 않도록 ID를 구분합니다.
	ImGui::PushID(BoneIndex);
	bool bChanged = false;
	bChanged |= ImGui::DragFloat3("Location", Transform.Location.V, 0.1f);
	// 드래그 중 값이 튀지 않도록 마지막 오일러 값을 기억한다
	static FRotationConversionCache RotationCache;
	FRotator Euler = RotationCache.QuatToRotator(Transform.Rotation);
	if (ImGui::DragFloat3("Rotation", Euler.V, 0.5f))
	{
		Transform.Rotation = RotationCache.RotatorToQuat(Euler);
		bChanged = true;
	}
	bChanged |= ImGui::DragFloat3("Scale", Transform.Scale.V, 0.01f);
	ImGui::PopID();

	if (bChanged)
	{
		Editor.SetSelectedBoneLocalTransform(Transform);
	}
}

void FSkeletalMeshDetailsPanel::DrawPhysicsAssetDetails()
{
	ImGui::SeparatorText("Physics Asset");
	ImGui::BeginDisabled(!Editor.GetMesh()->GetPhysicsAsset());
	if (ImGui::Button("Save Physics Asset"))
	{
		Editor.SavePhysicsAsset();
	}
	ImGui::EndDisabled();
	if (!Editor.GetPhysicsAssetStatus().empty())
	{
		ImGui::TextWrapped("%s", Editor.GetPhysicsAssetStatus().c_str());
	}

	const int32 BoneIndex = Editor.GetSelectedBoneIndex();
	const UBodySetup* Setup = Editor.GetSelectedBodySetup();
	if (!Setup || Setup->AggGeom.IsEmpty())
	{
		ImGui::TextDisabled("Select a body in Collider Hierarchy.");
		return;
	}

	ImGui::SeparatorText(Editor.GetBoneItems()[BoneIndex].Name.c_str());
	if (Setup->AggGeom.SphylElems.IsEmpty())
	{
		ImGui::TextDisabled("No capsule on this body.");
		return;
	}

	ImGui::PushID(BoneIndex);
	for (int32 Index = 0; Index < Setup->AggGeom.SphylElems.Num(); ++Index)
	{
		ImGui::PushID(Index);
		if (Setup->AggGeom.SphylElems.Num() > 1)
		{
			ImGui::Text("Capsule %d", Index + 1);
		}
		float Radius = Setup->AggGeom.SphylElems[Index].Radius;
		if (ImGui::DragFloat("Capsule Radius", &Radius, 0.01f, 0.0f, 0.0f, "%.5g"))
		{
			Editor.SetSelectedCapsuleRadius(Index, Radius);
		}
		ImGui::PopID();
	}
	ImGui::PopID();
}
