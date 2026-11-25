#include "EnginePCH.h"
#include "ConstraintGraphPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

namespace
{
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
	}
}

FConstraintGraphPanel::FConstraintGraphPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Constraint Graph")
{
}

bool FConstraintGraphPanel::IsAvailableInCurrentMode() const
{
	return Editor.IsPhysicsAssetMode();
}

void FConstraintGraphPanel::DrawContent()
{
	const UPhysicsAsset* Asset = Editor.GetMesh()->GetPhysicsAsset();
	const auto& Bones = Editor.GetMesh()->GetRefSkeleton();
	const int32 Focus = Editor.GetGraphFocusBone();
	const FConstraintSetup* SelectedJoint = Editor.GetSelectedConstraint();
	if (!Asset || (!Bones.IsValidIndex(Focus) && !SelectedJoint))
	{
		ImGui::TextWrapped("Select a body or joint to see its connections.");
		return;
	}

	ImGui::BeginDisabled(Editor.IsPhysicsPreviewRunning());
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
	ImGui::EndDisabled();
}
