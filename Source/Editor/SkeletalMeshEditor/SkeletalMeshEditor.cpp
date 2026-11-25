#include "EnginePCH.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "SkeletalMeshEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Render/SkeletalMesh.h"
#include <cmath>

bool FSkeletalMeshEditor::InitializeResources()
{
	if (!InitializePreview()) return false;
	BoneGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BoneGizmoTarget) return false;
	BoneGizmoTarget->DetachFromParent();
	return true;
}

void FSkeletalMeshEditor::ShutdownResources()
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	BoneGizmoTarget = nullptr;
	FSkeletalPreviewEditor::ShutdownResources();
}

void FSkeletalMeshEditor::TickAfterWorld(float DeltaTime)
{
	if (bPhysicsAssetMode)
	{
		return;
	}
	// 기즈모가 입력을 쓰지 않았을 때만 본을 피킹하고, 마지막에 기즈모를 선택한 본 위치로 맞춘다
	if (!UpdateBoneGizmo())
		UpdateBonePicking();
	SyncBoneGizmo();
}

TUniquePtr<IEditorTab> FSkeletalMeshEditor::CreateTab()
{
	return MakeUnique<FSkeletalMeshEditorTab>(*this);
}

void FSkeletalMeshEditor::SetPhysicsAssetMode(bool bEnabled)
{
	BoneGizmo.EndDrag();
	bPhysicsAssetMode = bEnabled;
	SyncBoneGizmo();
	if (bEnabled && !MeshComponent->GetOrCreatePhysicsAsset())
	{
		PhysicsAssetStatus = "No physics asset could be loaded or generated for this mesh.";
	}
	if (bEnabled)
	{
		const UBodySetup* Setup = GetSelectedBodySetup();
		if (!Setup || Setup->AggGeom.IsEmpty())
		{
			SelectBone(-1);
		}
	}
}

UBodySetup* FSkeletalMeshEditor::GetSelectedBodySetup() const
{
	UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!Asset || !Mesh->GetRefSkeleton().IsValidIndex(SelectedBoneIndex))
	{
		return nullptr;
	}

	// 트리 표시명은 FBX 네임스페이스가 생략될 수 있으므로 원본 본 이름을 쓴다.
	const int32 BodyIndex = Asset->FindBodyIndex(Mesh->GetRefSkeleton()[SelectedBoneIndex].Name);
	return BodyIndex >= 0 ? Asset->BodySetups[BodyIndex] : nullptr;
}

void FSkeletalMeshEditor::SetSelectedCapsuleRadius(int32 CapsuleIndex, float Radius)
{
	UBodySetup* Setup = GetSelectedBodySetup();
	if (!Setup || !Setup->AggGeom.SphylElems.IsValidIndex(CapsuleIndex) || !std::isfinite(Radius) || Radius <= 0.0f)
	{
		return;
	}

	Setup->AggGeom.SphylElems[CapsuleIndex].Radius = Radius;
	Mesh->GetPhysicsAsset()->bHasUnsavedChanges = true;
	PhysicsAssetStatus.clear();
}

bool FSkeletalMeshEditor::IsDirty() const
{
	const UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	return Asset && Asset->bHasUnsavedChanges;
}

void FSkeletalMeshEditor::SavePhysicsAsset()
{
	UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	const FString Path = FPhysicsAssetFile::GetDefaultPathForMesh(*Mesh);
	if (Path.empty())
	{
		PhysicsAssetStatus = "Cannot save: this mesh has no source file path.";
		return;
	}
	if (FPhysicsAssetFile::Write(Path, *Asset, PhysicsAssetStatus))
	{
		PhysicsAssetStatus = "Saved " + Asset->AssetPath;
	}
}

void FSkeletalMeshEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	if (!bPhysicsAssetMode)
	{
		FSkeletalPreviewEditor::OnRenderLine(LineBatcher);
		return;
	}

	const UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (Asset && MeshComponent)
	{
		const FVector4 BodyColor(0.35f, 0.58f, 0.68f, 1.0f);
		const FVector4 SelectedColor(0.1f, 0.9f, 1.0f, 1.0f);
		const float Scale = MeshComponent->GetPhysicsAssetScale();
		for (const UBodySetup* Setup : Asset->BodySetups)
		{
			const int32 BoneIndex = Setup ? MeshComponent->FindBoneIndex(Setup->BoneName) : -1;
			if (BoneIndex < 0)
			{
				continue;
			}
			const FTransform Bone = MatrixToBodyTransform(MeshComponent->GetBoneWorldMatrix(BoneIndex));
			const FVector4& Color = BoneIndex == SelectedBoneIndex ? SelectedColor : BodyColor;
			Setup->AggGeom.GetScaled(FVector(Scale, Scale, Scale)).DrawDebug(
				LineBatcher, Bone.Location, Bone.Rotation, Color);
		}
	}
}

void FSkeletalMeshEditor::SetGizmoMode(UE::Widget::EWidgetMode Mode)
{
	BoneGizmo.EndDrag();
	ModeTools.SetWidgetMode(Mode);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SetGizmoSpace(ECoordSystem Space)
{
	BoneGizmo.EndDrag();
	ModeTools.SetCoordSystem(Space);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SyncBoneGizmo()
{
	if (bPhysicsAssetMode || !MeshComponent || !BoneGizmoTarget || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}

	if (BoneGizmo.IsUsing())
	{
		return;
	}

	const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex);
	const FVector Location = BoneWorld.TransformPosition(FVector::ZeroVector);
	const FRotator Rotation = GetBoneWorldRotation(SelectedBoneIndex).ToFRotator();

	// 스케일은 조작 배율로만 사용합니다. 부모의 비균일 스케일과 전단을 축 회전에 섞지 않습니다.
	BoneGizmoTarget->SetTransform(FTransform(Rotation, Location));
	BoneGizmo.SetTarget(BoneGizmoTarget);
}

FQuat FSkeletalMeshEditor::GetBoneWorldRotation(int32 BoneIndex) const
{
	FQuat Rotation = FQuat::Identity();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	while (BoneIndex >= 0)
	{
		Rotation = MeshComponent->GetBoneLocalTransform(BoneIndex).Rotation * Rotation;
		BoneIndex = Bones[BoneIndex].ParentIndex;
	}

	return (MeshComponent->GetWorldRotation().Quaternion() * Rotation).Normalized();
}

bool FSkeletalMeshEditor::UpdateBoneGizmo()
{
	if
		(
			// 공통 뷰포트 입력 조건을 만족하지 않으면 입력을 무시
			!CanProcessViewportInput() ||

			// BoneGizmo의 대상이 없으면 입력을 무시
			!BoneGizmo.GetTarget()
			)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	if (PointerInput.bCancel)
	{
		if (BoneGizmo.IsUsing())
		{
			MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, DragStartBoneTransform);
			MeshComponent->RefreshBoneTransformsNow();
		}
		BoneGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = BoneGizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	SyncBoneGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 시작하고, 버튼을 놓는 프레임의 마지막 마우스 위치도 반영합니다.
	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	BoneGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && BoneGizmo.IsUsing())
	{
		const int32 ParentIndex = Mesh->GetRefSkeleton()[SelectedBoneIndex].ParentIndex;
		const FMatrix ParentWorld = ParentIndex >= 0
			? MeshComponent->GetBoneWorldMatrix(ParentIndex) : MeshComponent->GetWorldMatrix();

		// 스케일이 0인 부모의 역행렬로 이동을 계산할 수 없습니다.
		if (ModeTools.GetWidgetMode() == UE::Widget::WM_Translate && fabsf(ParentWorld.Determinant()) < 1e-12f)
		{
			BoneGizmo.EndDrag();
			return true;
		}

		DragStartBoneTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
		DragParentRotation = GetBoneWorldRotation(ParentIndex);
		if (ModeTools.GetWidgetMode() == UE::Widget::WM_Translate)
		{
			DragParentInverse = ParentWorld.Inverse();
		}
	}

	if (BoneGizmo.IsUsing())
	{
		ApplyBoneGizmoTransform();
		if (!PointerInput.bMouseDown)
		{
			BoneGizmo.EndDrag();
		}
	}

	return bWasUsing || BoneGizmo.IsUsing() || BoneGizmo.GetHoveredAxis() >= 0;
}

void FSkeletalMeshEditor::ApplyBoneGizmoTransform()
{
	FTransform Local = DragStartBoneTransform;
	switch (ModeTools.GetWidgetMode())
	{
	case UE::Widget::WM_Translate:
		Local.Location = DragParentInverse.TransformPosition(BoneGizmoTarget->GetWorldLocation());
		break;
	case UE::Widget::WM_Rotate:
		Local.Rotation = (DragParentRotation.Inverse() *
			BoneGizmoTarget->GetRelativeRotation().Quaternion()).Normalized();
		break;
	case UE::Widget::WM_Scale:
		Local.Scale = DragStartBoneTransform.Scale * BoneGizmoTarget->GetRelativeScale3D();
		break;
	}

	// 조작한 성분만 변경하고 나머지는 드래그 시작 시점의 로컬 값을 유지합니다.
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Local);
	MeshComponent->RefreshBoneTransformsNow();
}

bool FSkeletalMeshEditor::GetSelectedBoneLocalTransform(FTransform& OutTransform) const
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return false;
	}

	OutTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
	return true;
}

void FSkeletalMeshEditor::SetSelectedBoneLocalTransform(const FTransform& Transform)
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return;
	}

	// 원본 애셋 대신 프리뷰 컴포넌트의 포즈만 수정합니다. 본 갱신은 다음 World Tick에서 수행합니다.
	BoneGizmo.EndDrag();
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Transform);
}

void FSkeletalMeshEditor::ResetPose()
{
	if (!MeshComponent)
	{
		return;
	}

	BoneGizmo.EndDrag();
	MeshComponent->ResetPose();
}

void FSkeletalMeshEditor::ResetBone(int32 BoneIndex)
{
	if (!Mesh)
	{
		return;
	}

	if (!MeshComponent)
	{
		return;
	}

	if (BoneIndex < 0 || BoneIndex >= Mesh->GetRefSkeleton().Num())
	{
		return;
	}

	BoneGizmo.EndDrag();
	FTransform Transform = Mesh->GetRefSkeleton()[BoneIndex].LocalBindPose;
	MeshComponent->SetBoneLocalTransform(BoneIndex, Transform);
}
