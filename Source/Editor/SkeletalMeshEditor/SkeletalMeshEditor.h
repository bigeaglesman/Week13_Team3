#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/EditorModeManager.h"
#include "Editor/Viewports/EditorViewportClient.h"


class UBodySetup;
class UBoxComponent;
struct FConstraintSetup;
struct FKAggregateGeom;

enum class EColliderShape { None, Sphere, Box, Capsule };

class FSkeletalMeshEditor : public FSkeletalPreviewEditor
{
public:
	FSkeletalMeshEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InAssetPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
	{
		Mesh = &InMesh;
	}

	const char* GetEditorTypeName() const override { return "SkeletalMesh"; }
	const FGizmo* GetPreviewGizmo() const override { return BoneGizmo.GetTarget() ? &BoneGizmo : nullptr; }
	void OnRenderLine(FLineBatcher& LineBatcher) override;

	bool IsPhysicsAssetMode() const { return bPhysicsAssetMode; }
	void SetPhysicsAssetMode(bool bEnabled);
	void StartPhysicsPreview();
	void StopPhysicsPreview();
	float GetPreviewScale() const;
	void SetPreviewScale(float Scale);
	bool IsPhysicsPreviewRunning() const { return FloorCollision != nullptr; }
	bool ArePhysicsCountsVisible() const { return bShowPhysicsCounts; }
	void SetPhysicsCountsVisible(bool bVisible) { bShowPhysicsCounts = bVisible; }
	bool AreConstraintsVisible() const { return bShowConstraints; }
	void SetConstraintsVisible(bool bVisible);
	void SelectConstraint(int32 Index);
	void AddConstraintToSelectedBody(int32 ParentBone);
	void RemoveSelectedConstraint();
	void SelectBodyFromGraph(int32 BoneIndex);
	int32 GetGraphFocusBone() const { return GraphFocusBone; }
	FConstraintSetup* GetSelectedConstraint() const;
	UBodySetup* GetSelectedBodySetup() const;
	void SelectShape(EColliderShape Type, int32 Index);
	void AddBody(int32 BoneIndex);
	void RemoveSelectedBody();
	void AddShapeToSelectedBody(EColliderShape Type);
	bool CanRemoveSelectedShape() const;
	void RemoveSelectedShape();
	EColliderShape GetSelectedShapeType() const { return SelectedShapeType; }
	int32 GetSelectedShapeIndex() const { return SelectedShapeIndex; }
	unsigned GetGizmoModeMask() const;
	void MarkPhysicsAssetDirty();
	void SavePhysicsAsset();
	const FString& GetPhysicsAssetStatus() const { return PhysicsAssetStatus; }
	bool IsDirty() const override;

	// 선택한 본의 프리뷰 포즈를 부모 기준 로컬 변환으로 조회·수정합니다.
	bool GetSelectedBoneLocalTransform(FTransform& OutTransform) const;
	void SetSelectedBoneLocalTransform(const FTransform& Transform);
	void ResetPose();
	void ResetBone(int32 BoneIndex);

	void SetGizmoMode(UE::Widget::EWidgetMode Mode);
	void SetGizmoSpace(ECoordSystem Space);
	UE::Widget::EWidgetMode  GetGizmoMode() const { return ModeTools.GetWidgetMode(); }
	ECoordSystem GetGizmoSpace() const { return ModeTools.GetCoordSystem(); }
	bool IsGizmoDragging() const { return BoneGizmo.IsUsing(); }

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;     // InitializePreview() 후 BoneGizmoTarget 생성
	void ShutdownResources() override;       // 기즈모 정리 후 Super

	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return BoneGizmo.IsUsing(); }
	void OnBoneSelected() override;
	void CancelViewportInteraction() override { BoneGizmo.EndDrag(); }

private:
	bool bPhysicsAssetMode = false;
	bool bOtherModeShowCollision = true; // 모드를 오갈 때 각 모드의 뷰포트 표시 선택을 보존한다.
	bool bShowPhysicsCounts = true;
	UE::Widget::EWidgetMode MeshGizmoMode = UE::Widget::WM_Translate;
	bool bShowConstraints = true;
	int32 SelectedConstraintIndex = -1;
	EColliderShape SelectedShapeType = EColliderShape::None;
	int32 SelectedShapeIndex = -1;
	int32 GraphFocusBone = -1;
	bool bPhysicsPickPending = false;
	FVector2 PhysicsPickPosition;
	FString PhysicsAssetStatus;
	UBoxComponent* FloorCollision = nullptr;
	TArray<FTransform> PreviewStartPose;
	bool bPreviousSimulatePhysics = false;

	void SyncBoneGizmo();
	bool UpdateBoneGizmo();
	void ApplyBoneGizmoTransform();
	bool ApplyShapeGizmoDrag(FKAggregateGeom& Geom, const FTransform& Body, float GeometryScale) const;
	bool ResolveConstraintBones(const FConstraintSetup& Joint, int32& ParentBone, int32& ChildBone) const;
	bool ApplyConstraintGizmoDrag(const FConstraintSetup& Joint, FTransform& ParentFrame, FTransform& ChildFrame) const;
	FTransform DragStartGizmoWorld;
	FQuat GetBoneWorldRotation(int32 BoneIndex) const;

	USceneComponent* BoneGizmoTarget = nullptr;
	// 이 에디터의 편집 모드와 좌표계. 에셋 에디터는 레벨 에디터와 따로 하나씩 갖는다. BoneGizmo보다 먼저 선언해야 한다.
	FEditorModeTools ModeTools;
	FGizmo BoneGizmo{ &ModeTools };
	FTransform DragStartBoneTransform;
	FMatrix DragParentInverse;
	FQuat DragParentRotation;
};
