#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintSetup.h"
#include <cmath>

namespace
{
	bool DrawAngularLimitRow(const char* Axis, EConstraintMotion& Motion, float& LimitDeg)
	{
		// 기존 에셋의 Limited 0°는 PhysX에서도 잠긴 축이다.
		const EConstraintMotion EffectiveMotion = FConstraintSetup::AngularExtentDeg(Motion, LimitDeg) == 0.0f
			? EConstraintMotion::Locked : Motion;
		int Mode = static_cast<int>(EffectiveMotion);
		ImGui::PushID(Axis);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Axis);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
		bool Changed = ImGui::Combo("##Motion", &Mode, "Free\0Limited\0Locked\0");
		if (Changed)
		{
			Motion = static_cast<EConstraintMotion>(Mode);
			if (Motion == EConstraintMotion::Limited)
				LimitDeg = FMath::Clamp(LimitDeg, 1.0f, 179.0f);
		}
		if (Mode == static_cast<int>(EConstraintMotion::Limited))
		{
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-FLT_MIN);
			float Degrees = FConstraintSetup::AngularExtentDeg(Motion, LimitDeg);
			if (ImGui::DragFloat("##Degrees", &Degrees, 0.5f, 1.0f, 179.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp)
				&& std::isfinite(Degrees) && Degrees != LimitDeg)
			{
				LimitDeg = Degrees;
				Changed = true;
			}
		}
		ImGui::PopID();
		return Changed;
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
	if (FConstraintSetup* SelectedJoint = Editor.GetSelectedConstraint())
	{
		ImGui::SeparatorText("Angular Limits");
		const FString& Parent = SelectedJoint->ParentBoneName;
		const FString& Child = SelectedJoint->ChildBoneName;
		ImGui::TextWrapped("%s → %s", Parent.empty() ? "World" : Parent.c_str(), Child.empty() ? "World" : Child.c_str());
		ImGui::PushID(SelectedJoint);
		if (ImGui::BeginTable("##AngularLimits", 3))
		{
			ImGui::TableSetupColumn("Axis", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Motion", ImGuiTableColumnFlags_WidthStretch, 1.3f);
			ImGui::TableSetupColumn("Limit (±°)", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
			bool Changed = DrawAngularLimitRow("Twist", SelectedJoint->TwistMotion, SelectedJoint->TwistLimitDeg);
			Changed |= DrawAngularLimitRow("Swing 1", SelectedJoint->Swing1Motion, SelectedJoint->Swing1LimitDeg);
			Changed |= DrawAngularLimitRow("Swing 2", SelectedJoint->Swing2Motion, SelectedJoint->Swing2LimitDeg);
			if (Changed)
				Editor.MarkPhysicsAssetDirty();
			ImGui::EndTable();
		}
		ImGui::PopID();
		return;
	}

	if (const UBodySetup* Setup = Editor.GetSelectedBodySetup())
		DrawBodyCapsules(*Setup, Editor.GetSelectedBoneIndex());
	else
		ImGui::TextWrapped("Select a body or joint to edit its properties.");
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
