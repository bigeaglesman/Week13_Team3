#include "EnginePCH.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "SkeletalMeshEditorTab.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Map.h"
#include "Engine/World.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsScene.h"
#include "Render/LineBatcher.h"
#include "Render/SkeletalMesh.h"
#include <cmath>

namespace
{
	struct FShapePoseRef
	{
		FVector* Center = nullptr;
		FQuat* Rotation = nullptr;
	};

	FShapePoseRef FindShapePose(FKAggregateGeom& Geom, EColliderShape Type, int32 Index)
	{
		if (Type == EColliderShape::Sphere && Geom.SphereElems.IsValidIndex(Index))
			return { &Geom.SphereElems[Index].Center, nullptr };
		if (Type == EColliderShape::Box && Geom.BoxElems.IsValidIndex(Index))
			return { &Geom.BoxElems[Index].Center, &Geom.BoxElems[Index].Rotation };
		if (Type == EColliderShape::Capsule && Geom.SphylElems.IsValidIndex(Index))
			return { &Geom.SphylElems[Index].Center, &Geom.SphylElems[Index].Rotation };
		return {};
	}

	void DrawLimitArc(FLineBatcher& Lines, const FVector& Pivot, const FVector& Zero, const FVector& Tangent,
		float Radius, float ExtentDeg, const FVector4& Color)
	{
		if (ExtentDeg <= 0.0f || ExtentDeg >= 180.0f)
			return;
		const float HalfAngle = FMath::DegreesToRadians(ExtentDeg);
		constexpr int32 Segments = 32;
		FVector Previous = Pivot;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float Angle = HalfAngle * (2.0f * i / Segments - 1.0f);
			const FVector Point = Pivot + (Zero * std::cos(Angle) + Tangent * std::sin(Angle)) * Radius;
			Lines.AddLine(Previous, Point, Color);
			Previous = Point;
		}
		Lines.AddLine(Previous, Pivot, Color);
	}

	void DrawConstraintFrames(FLineBatcher& Lines, const FConstraintSetup& Joint, const FTransform& Parent,
		const FTransform& Child, float Scale, float Radius, bool bHighlighted)
	{
		const FVector ParentPivot = Parent.Location + Parent.Rotation.RotateVector(Joint.ParentFrame.Location * Scale);
		const FVector ChildPivot = Child.Location + Child.Rotation.RotateVector(Joint.ChildFrame.Location * Scale);
		const FQuat Axes = (Parent.Rotation * Joint.ParentFrame.Rotation).Normalized();
		const FVector X = Axes.GetForwardVector(), Y = Axes.GetRightVector(), Z = Axes.GetUpVector();
		const FVector4 SwingColor = bHighlighted ? FVector4(0.2f, 1.0f, 0.35f, 1) : FVector4(0.26f, 0.64f, 0.33f, 1);
		const FVector4 TwistColor = bHighlighted ? FVector4(1.0f, 0.35f, 0.17f, 1) : FVector4(0.73f, 0.34f, 0.22f, 1);
		const FVector4 DirectionColor(1.0f, 0.85f, 0.12f, 1);
		const FQuat ChildAxes = (Child.Rotation * Joint.ChildFrame.Rotation).Normalized();
		Lines.AddLine(ChildPivot, ChildPivot + ChildAxes.GetForwardVector() * Radius, DirectionColor);
		Lines.AddLine(ParentPivot, ParentPivot + Y * (Radius * 0.28f), TwistColor);
		Lines.AddLine(ParentPivot, ParentPivot + Z * (Radius * 0.28f), SwingColor);
		if ((ChildPivot - ParentPivot).SizeSquared() > Radius * Radius * 0.0001f)
			Lines.AddLine(ParentPivot, ChildPivot, DirectionColor);
		if (!bHighlighted)
			return;

		const float SwingY = Joint.AngularExtentDeg(Joint.Swing1Motion, Joint.Swing1LimitDeg);
		const float SwingZ = Joint.AngularExtentDeg(Joint.Swing2Motion, Joint.Swing2LimitDeg);
		if (SwingY > 0 && SwingY < 180 && SwingZ > 0 && SwingZ < 180)
		{
			// D6의 타원은 tan(회전각 / 4) 공간에 있다. 경계 회전으로 로컬 X축을 돌린다.
			const float TanY = std::tan(FMath::DegreesToRadians(SwingY) * 0.25f);
			const float TanZ = std::tan(FMath::DegreesToRadians(SwingZ) * 0.25f);
			constexpr int32 Segments = 32;
			FVector Previous;
			for (int32 i = 0; i <= Segments; ++i)
			{
				const float Angle = 2.0f * PI * i / Segments;
				const float U = TanY * std::cos(Angle), V = TanZ * std::sin(Angle);
				const float Sq = U * U + V * V;
				const float Inv = 1.0f / (1.0f + Sq);
				const FQuat Swing(0, 2.0f * U * Inv, 2.0f * V * Inv, (1.0f - Sq) * Inv);
				const FVector Point = ParentPivot + Axes.RotateVector(Swing.GetForwardVector()) * Radius;
				if (i > 0) Lines.AddLine(Previous, Point, SwingColor);
				if (i < Segments && i % (Segments / 4) == 0) Lines.AddLine(ParentPivot, Point, SwingColor);
				Previous = Point;
			}
		}
		else
		{
			DrawLimitArc(Lines, ParentPivot, X, Z, Radius, SwingY, SwingColor);
			DrawLimitArc(Lines, ParentPivot, X, Y, Radius, SwingZ, SwingColor);
		}
		const float Twist = Joint.AngularExtentDeg(Joint.TwistMotion, Joint.TwistLimitDeg);
		const float TwistRadius = Radius * 0.65f;
		DrawLimitArc(Lines, ParentPivot, Y, Z, TwistRadius, Twist, TwistColor);
		if (Twist > 0 && Twist < 180)
		{
			// Twist만 변하면 자식 X축은 그대로다. 부모 YZ 평면에 현재 비틀림도 표시한다.
			const FQuat Relative = Axes.UnitInverse() * ChildAxes;
			if (Relative.X * Relative.X + Relative.W * Relative.W > 1e-8f)
			{
				const FQuat TwistOnly = FQuat(Relative.X, 0, 0, Relative.W).Normalized();
				const FVector Direction = Axes.RotateVector(TwistOnly.GetRightVector());
				Lines.AddLine(ParentPivot, ParentPivot + Direction * TwistRadius, DirectionColor);
			}
		}
	}
}

bool FSkeletalMeshEditor::InitializeResources()
{
	// FBX 임포터는 cm로 베이크하고 물리 월드는 m 단위로 시뮬레이션한다.
	constexpr float CentimetersToMeters = 0.01f;
	if (!Mesh) return false;
	float PreviewScale = FFbxMeshBake::IsBakedPath(Mesh->GetPath()) ? CentimetersToMeters : 1.0f;
	if (const FEditorSettings* Settings = EditorEngine.GetEditorSettings())
	{
		if (const float* Saved = Settings->SkeletalPreviewScales.Find(Mesh->GetPath()))
			PreviewScale = *Saved;
	}
	if (!InitializePreview(PreviewScale)) return false;
	BoneGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BoneGizmoTarget) return false;
	BoneGizmoTarget->DetachFromParent();
	return true;
}

void FSkeletalMeshEditor::ShutdownResources()
{
	StopPhysicsPreview();
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	BoneGizmoTarget = nullptr;
	FSkeletalPreviewEditor::ShutdownResources();
}

float FSkeletalMeshEditor::GetPreviewScale() const
{
	return MeshComponent->GetRelativeScale3D().X;
}

void FSkeletalMeshEditor::SetPreviewScale(float Scale)
{
	if (IsPhysicsPreviewRunning() || !std::isfinite(Scale) || Scale <= 0.0f || Scale == GetPreviewScale())
		return;
	ApplyPreviewMeshScale(Scale);
	FocusMesh();
	SyncBoneGizmo();
	EditorEngine.SetSkeletalPreviewScale(Mesh->GetPath(), Scale);
}

void FSkeletalMeshEditor::TickAfterWorld(float DeltaTime)
{
	if (IsPhysicsPreviewRunning())
	{
		PreviewScene.GetWorld()->GetPhysicsScene()->Tick(DeltaTime);
		MeshComponent->RefreshBoneTransformsNow();
		return;
	}
	const bool bGizmoConsumed = UpdateBoneGizmo();
	if (bPhysicsAssetMode)
	{
		bPhysicsPickPending = !bGizmoConsumed && CanProcessViewportInput() && PointerInput.bHovered && PointerInput.bMousePressed
			&& !PointerInput.bCancel && !CameraInput.bCaptured;
		if (bPhysicsPickPending)
			PhysicsPickPosition = FVector2(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
	}
	else if (!bGizmoConsumed)
		UpdateBonePicking();
	SyncBoneGizmo();
}

TUniquePtr<IEditorTab> FSkeletalMeshEditor::CreateTab()
{
	return MakeUnique<FSkeletalMeshEditorTab>(*this);
}

void FSkeletalMeshEditor::SetPhysicsAssetMode(bool bEnabled)
{
	if (bPhysicsAssetMode == bEnabled) return;
	StopPhysicsPreview();
	BoneGizmo.EndDrag();
	if (bEnabled) MeshGizmoMode = GetGizmoMode();
	ModeTools.SetWidgetMode(bEnabled ? UE::Widget::WM_Translate : MeshGizmoMode);
	bPhysicsAssetMode = bEnabled;
	bPhysicsPickPending = false;
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
			return;
		}
	}
	OnBoneSelected();
}

void FSkeletalMeshEditor::StartPhysicsPreview()
{
	if (!bPhysicsAssetMode || IsPhysicsPreviewRunning()) return;
	const UPhysicsAsset* Asset = MeshComponent->GetOrCreatePhysicsAsset();
	if (!Asset || Asset->BodySetups.IsEmpty())
	{
		PhysicsAssetStatus = "Cannot simulate: the physics asset has no bodies.";
		return;
	}

	UWorld* World = PreviewScene.GetWorld();
	World->InitPhysicsScene();
	if (!World->GetPhysicsScene())
	{
		PhysicsAssetStatus = "Cannot simulate: the preview physics scene could not be created.";
		return;
	}
	if (!FloorMesh)
	{
		PhysicsAssetStatus = "Cannot simulate: the preview floor mesh is missing.";
		return;
	}
	FloorCollision = Cast<UBoxComponent>(PreviewScene.AddComponent(UBoxComponent::StaticClass()));
	if (!FloorCollision)
	{
		PhysicsAssetStatus = "Cannot simulate: the preview floor could not be created.";
		return;
	}

	bPhysicsPickPending = false;
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	PreviewStartPose = MeshComponent->GetBoneSpaceTransforms();
	bPreviousSimulatePhysics = MeshComponent->BodyInstance.bSimulatePhysics;
	MeshComponent->BodyInstance.bSimulatePhysics = true;
	MeshComponent->CreatePhysicsState();
	const auto& Bodies = MeshComponent->GetBodies();
	if (!MeshComponent->IsRagdollSimulating() || Bodies.Num() != Asset->BodySetups.Num()
		|| MeshComponent->GetNumConstraints() != Asset->ConstraintSetups.Num())
	{
		StopPhysicsPreview();
		PhysicsAssetStatus = "Cannot simulate: one or more asset bodies or joints could not be created for this mesh.";
		return;
	}

	// 표시 중인 평면 바로 아래에 박스를 두어 시각적인 바닥과 충돌 윗면을 일치시킨다.
	const FBox Bounds = FloorMesh->CalcBounds();
	const FVector HalfSize = (Bounds.Max - Bounds.Min) * 0.5f;
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	FloorCollision->BoxExtent = FVector(HalfSize.X, HalfSize.Y, HalfSize.X * 0.05f);
	FloorCollision->SetWorldLocation(FVector(Center.X, Center.Y, Bounds.Max.Z - FloorCollision->BoxExtent.Z), false);
	FloorCollision->BodyInstance.bEnablePhysicsCollision = true;
	FloorCollision->CreatePhysicsState();
	if (!FloorCollision->BodyInstance.IsValidBodyInstance())
	{
		StopPhysicsPreview();
		PhysicsAssetStatus = "Cannot simulate: the preview floor has no collision body.";
		return;
	}
	PhysicsAssetStatus.clear();
}

void FSkeletalMeshEditor::StopPhysicsPreview()
{
	if (!IsPhysicsPreviewRunning()) return;
	MeshComponent->DestroyPhysicsState();
	MeshComponent->BodyInstance.bSimulatePhysics = bPreviousSimulatePhysics;
	PreviewScene.GetWorld()->DestroyActor(FloorCollision->GetOwner());
	FloorCollision = nullptr;
	for (int32 i = 0; i < PreviewStartPose.Num(); ++i)
		MeshComponent->SetBoneLocalTransform(i, PreviewStartPose[i]);
	PreviewStartPose.Empty();
	MeshComponent->RefreshBoneTransformsNow();
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::OnBoneSelected()
{
	SelectedConstraintIndex = -1;
	GraphFocusBone = bPhysicsAssetMode ? SelectedBoneIndex : -1;
	if (const UBodySetup* Setup = bPhysicsAssetMode ? GetSelectedBodySetup() : nullptr)
	{
		const auto& Geom = Setup->AggGeom;
		if (Geom.SphereElems.Num() + Geom.BoxElems.Num() + Geom.SphylElems.Num()
			+ Geom.ConvexElems.Num() + Geom.TriMeshElems.Num() == 1)
		{
			if (!Geom.SphereElems.IsEmpty()) return SelectShape(EColliderShape::Sphere, 0);
			if (!Geom.BoxElems.IsEmpty()) return SelectShape(EColliderShape::Box, 0);
			if (!Geom.SphylElems.IsEmpty()) return SelectShape(EColliderShape::Capsule, 0);
		}
	}
	SelectShape(EColliderShape::None, -1);
}

void FSkeletalMeshEditor::SelectBodyFromGraph(int32 BoneIndex)
{
	const int32 PreviousFocus = GraphFocusBone;
	SelectBone(BoneIndex);
	GraphFocusBone = PreviousFocus;
}

void FSkeletalMeshEditor::SelectConstraint(int32 Index)
{
	SelectBodyFromGraph(-1);
	SelectedConstraintIndex = Index;
	bShowConstraints = true;
}

void FSkeletalMeshEditor::SetConstraintsVisible(bool bVisible)
{
	bShowConstraints = bVisible;
	if (!bVisible)
		SelectedConstraintIndex = -1;
}

FConstraintSetup* FSkeletalMeshEditor::GetSelectedConstraint() const
{
	UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	return Asset && Asset->ConstraintSetups.IsValidIndex(SelectedConstraintIndex) ? &Asset->ConstraintSetups[SelectedConstraintIndex] : nullptr;
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

void FSkeletalMeshEditor::SelectShape(EColliderShape Type, int32 Index)
{
	BoneGizmo.EndDrag();
	SelectedShapeType = Type;
	SelectedShapeIndex = Index;
	SyncBoneGizmo();
}

unsigned FSkeletalMeshEditor::GetGizmoModeMask() const
{
	using namespace UE::Widget;
	if (!bPhysicsAssetMode) return AllTransformModes;
	if (IsPhysicsPreviewRunning() || SelectedShapeType == EColliderShape::None) return 0;
	return SelectedShapeType == EColliderShape::Sphere ? ModeBit(WM_Translate) | ModeBit(WM_Scale) : AllTransformModes;
}

void FSkeletalMeshEditor::MarkPhysicsAssetDirty()
{
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
		const FVector ViewForward = ViewportClient->GetViewForward();
		const bool bPick = bPhysicsPickPending;
		bPhysicsPickPending = false;
		int32 HitBody = -1, HitConstraint = -1;
		EColliderShape HitShape = EColliderShape::None;
		int32 HitShapeIndex = -1;
		float BodyDepth = FLT_MAX, JointDepth = FLT_MAX;
		FRay Ray;
		if (bPick)
			Ray = ViewportClient->DeprojectScreenToWorld(PhysicsPickPosition);
		TMap<FString, FTransform> BodyPoses;
		FKAggregateGeom SingleShape;
		for (const UBodySetup* Setup : Asset->BodySetups)
		{
			const int32 BoneIndex = Setup ? MeshComponent->FindBoneIndex(Setup->BoneName) : -1;
			if (BoneIndex < 0)
			{
				continue;
			}
			const FTransform Bone = MatrixToBodyTransform(MeshComponent->GetBoneWorldMatrix(BoneIndex));
			if (bShowConstraints && !Setup->AggGeom.IsEmpty())
			{
				BodyPoses.Add(Setup->BoneName, Bone);
			}
			FKAggregateGeom Geom = Setup->AggGeom.GetScaled(FVector(Scale, Scale, Scale));
			const FVector ViewDirection = ViewportClient->IsPerspective()
				? (Bone.Location - ViewportClient->GetViewLocation()).GetSafeNormal(SMALL_NUMBER, ViewForward)
				: ViewForward;
			const bool bSelectedBody = BoneIndex == SelectedBoneIndex;
			if (!bPick && (!bSelectedBody || SelectedShapeType == EColliderShape::None))
			{
				Geom.DrawDebug(LineBatcher, Bone.Location, Bone.Rotation, bSelectedBody ? SelectedColor : BodyColor, &ViewDirection);
				continue;
			}
			// 드래그 중에는 렌더용 사본만 바꾸고, 버튼을 놓을 때 에셋에 기록한다.
			if (bSelectedBody && BoneGizmo.IsUsing())
				ApplyShapeGizmoDrag(Geom, Bone, Scale);
			FRay LocalRay;
			if (bPick)
			{
				const FQuat Inverse = Bone.Rotation.UnitInverse();
				LocalRay = { Inverse.RotateVector(Ray.Origin - Bone.Location), Inverse.RotateVector(Ray.Direction) };
			}
			auto DrawAndPick = [&](const FKAggregateGeom& Shape, EColliderShape Type, int32 Index)
			{
				const bool bSelected = bSelectedBody && (SelectedShapeType == EColliderShape::None
					|| (SelectedShapeType == Type && SelectedShapeIndex == Index));
				const uint32 First = LineBatcher.GetVertexCount();
				Shape.DrawDebug(LineBatcher, Bone.Location, Bone.Rotation, bSelected ? SelectedColor : BodyColor, &ViewDirection);
				if (!bPick) return;
				float Depth = PickWireDepth(LineBatcher, First, PhysicsPickPosition);
				float Distance;
				if (Shape.RaycastPrimitives(LocalRay, Distance))
				{
					const FVector4 Clip = FVector4(Ray.PointAt(Distance), 1) * ViewportClient->GetViewProjectionMatrix();
					if (Clip.W > 0.0001f && Clip.Z >= 0 && Clip.Z <= Clip.W)
						Depth = std::min(Depth, Clip.Z / Clip.W);
				}
				if (Depth < BodyDepth)
				{
					BodyDepth = Depth;
					HitBody = BoneIndex;
					HitShape = Type;
					HitShapeIndex = Index;
				}
			};
			auto DrawPrimitives = [&](auto& Elements, auto& Scratch, EColliderShape Type)
			{
				if (Elements.IsEmpty()) return;
				Scratch.SetNum(1);
				for (int32 i = 0; i < Elements.Num(); ++i)
				{
					Scratch[0] = Elements[i];
					DrawAndPick(SingleShape, Type, i);
				}
				Scratch.Reset();
				Elements.Reset();
			};
			DrawPrimitives(Geom.SphereElems, SingleShape.SphereElems, EColliderShape::Sphere);
			DrawPrimitives(Geom.BoxElems, SingleShape.BoxElems, EColliderShape::Box);
			DrawPrimitives(Geom.SphylElems, SingleShape.SphylElems, EColliderShape::Capsule);
			if (!Geom.IsEmpty()) DrawAndPick(Geom, EColliderShape::None, -1);
		}
		if (bShowConstraints)
		{
			const float Radius = BoneSphereSize * 7.0f;
			for (int32 i = 0; i < Asset->ConstraintSetups.Num(); ++i)
			{
				const FConstraintSetup& Joint = Asset->ConstraintSetups[i];
				const FTransform* Parent = BodyPoses.Find(Joint.ParentBoneName);
				const FTransform* Child = BodyPoses.Find(Joint.ChildBoneName);
				if (Parent && Child)
				{
					const bool bHighlighted = i == SelectedConstraintIndex;
					const uint32 First = LineBatcher.GetVertexCount();
					DrawConstraintFrames(LineBatcher, Joint, *Parent, *Child, Scale, bHighlighted ? Radius * 1.5f : Radius, bHighlighted);
					if (bPick)
					{
						const float Depth = PickWireDepth(LineBatcher, First, PhysicsPickPosition);
						if (Depth < JointDepth)
						{
							JointDepth = Depth;
							HitConstraint = i;
						}
					}
				}
			}
		}
		if (bPick)
		{
			if (HitConstraint >= 0)
			{
				SelectConstraint(HitConstraint);
				GraphFocusBone = Mesh->FindBoneIndex(Asset->ConstraintSetups[HitConstraint].ChildBoneName);
			}
			else
			{
				SelectBone(HitBody);
				if (HitShape != EColliderShape::None)
					SelectShape(HitShape, HitShapeIndex);
			}
		}
	}
}

void FSkeletalMeshEditor::SetGizmoMode(UE::Widget::EWidgetMode Mode)
{
	if (bPhysicsAssetMode && !(GetGizmoModeMask() & UE::Widget::ModeBit(Mode))) return;
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
	if (IsPhysicsPreviewRunning() || !MeshComponent || !BoneGizmoTarget || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}

	if (BoneGizmo.IsUsing())
	{
		return;
	}

	const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex);
	if (bPhysicsAssetMode)
	{
		UBodySetup* Setup = GetSelectedBodySetup();
		const FShapePoseRef Pose = Setup ? FindShapePose(Setup->AggGeom, SelectedShapeType, SelectedShapeIndex) : FShapePoseRef{};
		if (!Pose.Center)
		{
			BoneGizmo.SetTarget(nullptr);
			return;
		}
		const FTransform Body = MatrixToBodyTransform(BoneWorld);
		if (!(GetGizmoModeMask() & UE::Widget::ModeBit(GetGizmoMode())))
			ModeTools.SetWidgetMode(UE::Widget::WM_Translate);
		const FVector Center = Body.Location + Body.Rotation.RotateVector(*Pose.Center * MeshComponent->GetPhysicsAssetScale());
		const FQuat Rotation = Pose.Rotation ? Body.Rotation * *Pose.Rotation : Body.Rotation;
		BoneGizmoTarget->SetTransform(FTransform(Rotation, Center));
		BoneGizmo.SetTarget(BoneGizmoTarget);
		return;
	}
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
		if (BoneGizmo.IsUsing() && !bPhysicsAssetMode)
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

	if (bPhysicsAssetMode && BoneGizmo.IsUsing())
	{
		if (!bWasUsing)
			DragStartGizmoWorld = BoneGizmoTarget->GetTransform();
		if (!PointerInput.bMouseDown)
		{
			const FTransform Body = MatrixToBodyTransform(MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex));
			UBodySetup* Setup = GetSelectedBodySetup();
			if (Setup && ApplyShapeGizmoDrag(Setup->AggGeom, Body, 1.0f))
				MarkPhysicsAssetDirty();
			BoneGizmo.EndDrag();
		}
		return true;
	}
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

bool FSkeletalMeshEditor::ApplyShapeGizmoDrag(FKAggregateGeom& Geom, const FTransform& Body, float GeometryScale) const
{
	const FShapePoseRef Pose = FindShapePose(Geom, SelectedShapeType, SelectedShapeIndex);
	if (!Pose.Center) return false;
	const FTransform& World = BoneGizmoTarget->GetTransform();
	if (GetGizmoMode() == UE::Widget::WM_Translate)
	{
		const float Scale = MeshComponent->GetPhysicsAssetScale();
		if (Scale <= 0.0f || World.Location == DragStartGizmoWorld.Location) return false;
		*Pose.Center = Body.Rotation.UnitInverse().RotateVector(World.Location - Body.Location) * (GeometryScale / Scale);
		return true;
	}
	if (GetGizmoMode() == UE::Widget::WM_Rotate && Pose.Rotation)
	{
		const FQuat Delta = World.Rotation * DragStartGizmoWorld.Rotation.UnitInverse();
		if (FVector(Delta.X, Delta.Y, Delta.Z).SizeSquared() < 1e-12f) return false;
		*Pose.Rotation = (Body.Rotation.UnitInverse() * World.Rotation).Normalized();
		return true;
	}
	if (GetGizmoMode() != UE::Widget::WM_Scale) return false;

	// 기즈모 타깃은 매 드래그를 단위 스케일에서 시작한다. 축 손잡이는 한 성분, 중앙은 세 성분을 같은 비율로 바꾼다.
	const FVector Factor = World.Scale;
	if (Factor == FVector::OneVector) return false;
	switch (SelectedShapeType)
	{
	case EColliderShape::Sphere:
	{
		auto& Sphere = Geom.SphereElems[SelectedShapeIndex];
		const float AxisFactor = Factor.X != 1.0f ? Factor.X : (Factor.Y != 1.0f ? Factor.Y : Factor.Z);
		const float Radius = Sphere.Radius * AxisFactor;
		if (!std::isfinite(Radius) || Radius <= 0.0f || Radius == Sphere.Radius) return false;
		Sphere.Radius = Radius;
		return true;
	}
	case EColliderShape::Box:
	{
		auto& Box = Geom.BoxElems[SelectedShapeIndex];
		const FVector HalfExtent = Box.HalfExtent * Factor;
		if (!std::isfinite(HalfExtent.X) || !std::isfinite(HalfExtent.Y) || !std::isfinite(HalfExtent.Z)
			|| HalfExtent.X <= 0 || HalfExtent.Y <= 0 || HalfExtent.Z <= 0 || HalfExtent == Box.HalfExtent) return false;
		Box.HalfExtent = HalfExtent;
		return true;
	}
	case EColliderShape::Capsule:
	{
		auto& Capsule = Geom.SphylElems[SelectedShapeIndex];
		const float RadialFactor = Factor.X != 1.0f ? Factor.X : Factor.Y;
		const float Radius = Capsule.Radius * RadialFactor;
		float Length = Capsule.Length * Factor.Z;
		// Z만 잡으면 끝에서 끝까지의 높이를 조절한다. 원통 길이가 0인 캡슐도 늘릴 수 있다.
		if (Factor.Z != 1.0f && RadialFactor == 1.0f)
			Length = std::max(0.0f, Length + Capsule.Radius * (Factor.Z - 1.0f) * 2.0f);
		if (!std::isfinite(Radius) || !std::isfinite(Length) || Radius <= 0.0f
			|| (Radius == Capsule.Radius && Length == Capsule.Length)) return false;
		Capsule.Radius = Radius;
		Capsule.Length = Length;
		return true;
	}
	default: break;
	}
	return false;
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
