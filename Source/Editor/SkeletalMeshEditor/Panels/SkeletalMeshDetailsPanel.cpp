#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include <format>

namespace
{
	FString FormatAngularExtent(EConstraintMotion Motion, float Degrees)
	{
		if (Motion == EConstraintMotion::Free)
			return "Free";
		const float Extent = FConstraintSetup::AngularExtentDeg(Motion, Degrees);
		return Extent == 0.0f ? "Locked" : std::format("±{:g}°", Extent);
	}

	void DrawBodyNode(FSkeletalMeshEditor& Editor, const FString& Name, float Width)
	{
		const int32 Index = Editor.GetMesh()->FindBoneIndex(Name);
		const bool bValid = Editor.GetBoneItems().IsValidIndex(Index);
		const char* Label = bValid ? Editor.GetBoneItems()[Index].Name.c_str() : Name.empty() ? "World" : Name.c_str();
		const bool bSelected = bValid && Index == Editor.GetSelectedBoneIndex();
		ImGui::BeginDisabled(!bValid);
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(bSelected ? ImGuiCol_ButtonActive : ImGuiCol_Button));
		const bool bClicked = ImGui::Button(Label, ImVec2(Width, 0));
		if (bValid && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			Editor.SelectBone(Index);
		else if (bClicked && !bSelected)
			Editor.SelectBodyFromGraph(Index);
		ImGui::PopStyleColor();
		ImGui::EndDisabled();
		if (!Name.empty())
			ImGui::SetItemTooltip("%s\nDouble-click to focus connections", Name.c_str());
	}

	void DrawJointConnection(FSkeletalMeshEditor& Editor, const FConstraintSetup& Joint, int32 JointIndex, bool bSelected)
	{
		const float Gap = ImGui::GetStyle().ItemSpacing.x;
		const float JointWidth = ImGui::CalcTextSize("Joint").x + ImGui::GetStyle().FramePadding.x * 2;
		const float BodyWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - JointWidth - Gap * 2) * 0.5f);
		const ImVec2 Start = ImGui::GetCursorScreenPos();
		const float MidY = Start.y + ImGui::GetFrameHeight() * 0.5f;
		const ImU32 LineColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const float ParentEnd = Start.x + BodyWidth;
		const float JointEnd = ParentEnd + Gap + JointWidth;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(ParentEnd, MidY), ImVec2(ParentEnd + Gap, MidY), LineColor);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(JointEnd, MidY), ImVec2(JointEnd + Gap, MidY), LineColor);

		ImGui::PushID(JointIndex);
		ImGui::PushID("Parent");
		DrawBodyNode(Editor, Joint.ParentBoneName, BodyWidth);
		ImGui::PopID();
		ImGui::SameLine(0, Gap);
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(bSelected ? ImGuiCol_ButtonActive : ImGuiCol_Header));
		if (ImGui::Button("Joint", ImVec2(JointWidth, 0)))
			Editor.SelectConstraint(JointIndex);
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("%s", Joint.JointName.c_str());
		ImGui::SameLine(0, Gap);
		ImGui::PushID("Child");
		DrawBodyNode(Editor, Joint.ChildBoneName, BodyWidth);
		ImGui::PopID();
		ImGui::PopID();

		ImGui::TextWrapped("Twist %s   Swing 1 %s   Swing 2 %s",
			FormatAngularExtent(Joint.TwistMotion, Joint.TwistLimitDeg).c_str(),
			FormatAngularExtent(Joint.Swing1Motion, Joint.Swing1LimitDeg).c_str(),
			FormatAngularExtent(Joint.Swing2Motion, Joint.Swing2LimitDeg).c_str());
	}
}

FSkeletalMeshDetailsPanel::FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Details")
{
}

void FSkeletalMeshDetailsPanel::DrawContent()
{
	if (Editor.IsPhysicsAssetMode())
	{
		ImGui::BeginDisabled(Editor.IsPhysicsPreviewRunning());
		DrawPhysicsAssetDetails();
		ImGui::EndDisabled();
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

	const FConstraintSetup* SelectedJoint = Editor.GetSelectedConstraint();
	const auto& Bones = Editor.GetMesh()->GetRefSkeleton();
	const int32 Focus = Editor.GetGraphFocusBone();
	const UPhysicsAsset* Asset = Editor.GetMesh()->GetPhysicsAsset();
	if (!Asset || (!Bones.IsValidIndex(Focus) && !SelectedJoint))
	{
		ImGui::TextWrapped("Select a body or constraint in the viewport, or a body in Collider Hierarchy.");
		return;
	}

	ImGui::SeparatorText("Constraint Graph");
	bool bHasConnection = false;
	const auto& Joints = Asset->ConstraintSetups;
	for (int32 i = 0; i < Joints.Num(); ++i)
	{
		const FConstraintSetup& Joint = Joints[i];
		const bool bVisible = Bones.IsValidIndex(Focus)
			? Joint.ParentBoneName == Bones[Focus].Name || Joint.ChildBoneName == Bones[Focus].Name
			: &Joint == SelectedJoint;
		if (bVisible)
		{
			if (bHasConnection)
				ImGui::Separator();
			DrawJointConnection(Editor, Joint, i, &Joint == SelectedJoint);
			bHasConnection = true;
		}
	}
	if (!bHasConnection)
		ImGui::TextDisabled("No connected constraints.");

	if (const UBodySetup* Setup = Editor.GetSelectedBodySetup())
		DrawBodyCapsules(*Setup, Editor.GetSelectedBoneIndex());
}

void FSkeletalMeshDetailsPanel::DrawBodyCapsules(const UBodySetup& Setup, int32 BoneIndex)
{
	ImGui::SeparatorText(Editor.GetBoneItems()[BoneIndex].Name.c_str());
	if (Setup.AggGeom.SphylElems.IsEmpty())
	{
		ImGui::TextDisabled("No capsule on this body.");
	}

	ImGui::PushID(BoneIndex);
	for (int32 Index = 0; Index < Setup.AggGeom.SphylElems.Num(); ++Index)
	{
		ImGui::PushID(Index);
		if (Setup.AggGeom.SphylElems.Num() > 1)
		{
			ImGui::Text("Capsule %d", Index + 1);
		}
		float Radius = Setup.AggGeom.SphylElems[Index].Radius;
		if (ImGui::DragFloat("Capsule Radius", &Radius, 0.01f, 0.0f, 0.0f, "%.5g"))
		{
			Editor.SetSelectedCapsuleRadius(Index, Radius);
		}
		ImGui::PopID();
	}
	ImGui::PopID();

}
