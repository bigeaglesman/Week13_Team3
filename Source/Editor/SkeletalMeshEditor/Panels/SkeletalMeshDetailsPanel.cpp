#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintSetup.h"
#include <cmath>
#include <cstdio>

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

	if (UBodySetup* Setup = Editor.GetSelectedBodySetup())
		DrawBodyShapes(*Setup, Editor.GetSelectedBoneIndex());
	else
		ImGui::TextWrapped("Select a body or joint to edit its properties.");
}

void FSkeletalMeshDetailsPanel::DrawBodyShapes(UBodySetup& Setup, int32 BoneIndex)
{
	ImGui::SeparatorText(Editor.GetBoneItems()[BoneIndex].Name.c_str());
	auto& Geom = Setup.AggGeom;
	struct FShapeGroup { EColliderShape Type; const char* Label; int32 Count; };
	const FShapeGroup Groups[] = {
		{ EColliderShape::Sphere, "Sphere", Geom.SphereElems.Num() },
		{ EColliderShape::Box, "Box", Geom.BoxElems.Num() },
		{ EColliderShape::Capsule, "Capsule", Geom.SphylElems.Num() }
	};
	const int32 PrimitiveCount = Geom.SphereElems.Num() + Geom.BoxElems.Num() + Geom.SphylElems.Num();
	ImGui::PushID(BoneIndex);
	if (PrimitiveCount == 0)
	{
		ImGui::TextDisabled("No editable primitive on this body.");
	}
	else if (PrimitiveCount + Geom.ConvexElems.Num() + Geom.TriMeshElems.Num() > 1)
	{
		char Preview[48] = "Select shape";
		for (const auto& Group : Groups)
			if (Group.Type == Editor.GetSelectedShapeType())
				std::snprintf(Preview, sizeof(Preview), "%s %d", Group.Label, Editor.GetSelectedShapeIndex() + 1);
		if (ImGui::BeginCombo("Shape", Preview))
		{
			for (const auto& Group : Groups)
			{
				for (int32 i = 0; i < Group.Count; ++i)
				{
					char Label[48];
					std::snprintf(Label, sizeof(Label), "%s %d", Group.Label, i + 1);
					if (ImGui::Selectable(Label, Group.Type == Editor.GetSelectedShapeType() && i == Editor.GetSelectedShapeIndex()))
						Editor.SelectShape(Group.Type, i);
				}
			}
			ImGui::EndCombo();
		}
	}
	const int32 Index = Editor.GetSelectedShapeIndex();
	ImGui::PushID(Index);
	auto DrawDimension = [&](const char* Label, float& Value, bool bAllowZero)
	{
		float Edited = Value;
		if (ImGui::DragFloat(Label, &Edited, 0.01f, 0.0f, 0.0f, "%.5g")
			&& std::isfinite(Edited) && (bAllowZero ? Edited >= 0.0f : Edited > 0.0f) && Edited != Value)
		{
			Value = Edited;
			Editor.MarkPhysicsAssetDirty();
		}
	};
	switch (Editor.GetSelectedShapeType())
	{
	case EColliderShape::Capsule:
		if (!Geom.SphylElems.IsValidIndex(Index)) break;
		DrawDimension("Capsule Radius", Geom.SphylElems[Index].Radius, false);
		DrawDimension("Cylinder Length", Geom.SphylElems[Index].Length, true);
		break;
	case EColliderShape::Sphere:
		if (!Geom.SphereElems.IsValidIndex(Index)) break;
		DrawDimension("Sphere Radius", Geom.SphereElems[Index].Radius, false);
		break;
	case EColliderShape::Box:
	{
		if (!Geom.BoxElems.IsValidIndex(Index)) break;
		FVector Size = Geom.BoxElems[Index].HalfExtent * 2.0f;
		if (ImGui::DragFloat3("Box Size", Size.V, 0.01f) && Size.X > 0 && Size.Y > 0 && Size.Z > 0
			&& std::isfinite(Size.X) && std::isfinite(Size.Y) && std::isfinite(Size.Z))
		{
			Geom.BoxElems[Index].HalfExtent = Size * 0.5f;
			Editor.MarkPhysicsAssetDirty();
		}
		break;
	}
	default: break;
	}
	ImGui::PopID();

	if (ImGui::Button("Add Shape"))
		ImGui::OpenPopup("AddShapeMenu");
	if (ImGui::BeginPopup("AddShapeMenu"))
	{
		for (const auto& Group : Groups)
		{
			if (ImGui::MenuItem(Group.Label))
				Editor.AddShapeToSelectedBody(Group.Type);
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	const bool bCanRemove = Editor.CanRemoveSelectedShape();
	ImGui::BeginDisabled(!bCanRemove);
	if (ImGui::Button("Remove Shape"))
		Editor.RemoveSelectedShape();
	ImGui::EndDisabled();
	if (!bCanRemove && !Editor.IsPhysicsPreviewRunning() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", Editor.GetSelectedShapeType() == EColliderShape::None
			? "Select a shape to remove." : "Keep at least one saved collision shape on this body.");
	ImGui::PopID();
}
