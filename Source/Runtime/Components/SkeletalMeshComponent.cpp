#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Render/LineBatcher.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"
#include "Physics/AggregateGeom.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/ConstraintSetup.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsAssetFile.h"
#include "ClothingSystem/ClothingSimulationNv.h"
#include "ClothingSystem/ClothingAssetCommon.h"
#include "ClothingSystem/ClothingAssetFactory.h"
#include "Render/SkeletalMeshObject.h"

UClothingAssetCommon* USkeletalMeshComponent::GetOrCreateClothingAsset() const
{
	USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh || ClothSectionIndex < 0)
	{
		return nullptr;
	}

	// 메시가 소유하므로 같은 메시를 쓰는 다른 컴포넌트와 공유하고, 다음부터는 재사용된다. (GetOrCreatePhysicsAsset과 같은 방식)
	if (UClothingAssetCommon* Existing = Mesh->GetSectionClothingAsset(ClothSectionIndex))
	{
		return Existing;
	}

	// 실패한 섹션은 다시 시도하지 않는다 (매 프레임 경고가 쌓이지 않게). 번호를 바꾸면 다시 시도한다.
	if (ClothSectionIndex == FailedClothSectionIndex)
	{
		return nullptr;
	}

	FSkeletalMeshClothBuildParams Params;
	Params.SourceSection = ClothSectionIndex;
	Params.PinHeightRatio = ClothPinHeightRatio;
	UClothingAssetCommon* Asset = FClothingAssetFactory::CreateFromSkeletalMesh(Mesh, Params);
	if (!Asset)
	{
		FailedClothSectionIndex = ClothSectionIndex;
	}
	return Asset;
}

void USkeletalMeshComponent::OnRegister()
{
	Super::OnRegister();

	// 메시에 천이 있든 없든 Solver는 하나 만들어 둔다 (빈 Solver는 Simulate에서 바로 돌아온다). UE도 같다.
	// NvCloth 초기화가 실패했으면 Initialize가 false라서 천 없이 돈다.
	ClothingSimulation = new FClothingSimulationNv();
	if (!ClothingSimulation->Initialize())
	{
		delete ClothingSimulation;
		ClothingSimulation = nullptr;
		return;
	}
	ClothingSimulationContext = new FClothingSimulationContextNv();
}

void USkeletalMeshComponent::OnUnregister()
{
	// 컨텍스트, 시뮬레이션 순으로 지운다. Solver는 시뮬레이션 소멸자(Shutdown)에서 지워진다.
	delete ClothingSimulationContext;
	ClothingSimulationContext = nullptr;
	delete ClothingSimulation;
	ClothingSimulation = nullptr;

	Super::OnUnregister();
}

void USkeletalMeshComponent::TickClothing(float DeltaTime)
{
	if (!ClothingSimulation || !ClothingSimulationContext || !GetSkeletalMesh())
	{
		return;
	}

	ClothingSimulation->FillContext(this, DeltaTime, *ClothingSimulationContext);

	// 쓸 천 에셋이 바뀌었으면(처음, 섹션 번호 변경) 천을 다시 만든다. UE의 RecreateClothingActors
	UClothingAssetCommon* Asset = GetOrCreateClothingAsset();
	if (!Asset)
	{
		if (!ClothingSimulation->GetActors().IsEmpty())
		{
			ClothingSimulation->DestroyActors();
			SendClothSimulDataToMeshObject();   // 천 정점이 스키닝 결과로 돌아가게 비운다
		}
		return;
	}
	if (!ClothingSimulation->HasActorForAsset(Asset))
	{
		ClothingSimulation->DestroyActors();
		if (ClothSectionIndex == FailedClothSectionIndex || !ClothingSimulation->CreateActor(Asset, GetClothCollisionPhysicsAsset(), *ClothingSimulationContext))
		{
			FailedClothSectionIndex = ClothSectionIndex;   // 쿠킹 실패도 매 프레임 다시 시도하지 않는다
			return;
		}
		ClothingSimulation->FillContext(this, DeltaTime, *ClothingSimulationContext);   // 새 천은 첫 프레임을 텔레포트로
	}

	ClothingSimulation->SetConfig(ClothConfig);
	ClothingSimulation->Simulate(*ClothingSimulationContext);

	// 결과를 메시 오브젝트에 넘긴다. 바로 뒤의 SendRenderDynamicData → Update에서 천 정점에 덮어써 올린다.
	SendClothSimulDataToMeshObject();
}

void USkeletalMeshComponent::SendClothSimulDataToMeshObject()
{
	FSkeletalMeshObject* MeshObject = GetMeshObject();
	if (!MeshObject)
	{
		return;
	}

	TArray<FClothSimulData> Data;
	if (ClothingSimulation)
	{
		ClothingSimulation->GetSimulationData(Data, GetSkeletalMesh());
	}
	MeshObject->SetClothSimulData(std::move(Data));
}

UPhysicsAsset* USkeletalMeshComponent::GetOrCreatePhysicsAsset(bool bGenerateIfMissing) const
{
	USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh)
	{
		return nullptr;
	}

	// 메시 옆에 저장된 .physicsasset(에디터에서 만든 것)이 있으면 읽고, 없으면 뼈 모양으로 자동 생성한다.
	// 메시가 소유하므로 다음부터는 재사용된다.
	if (!Mesh->GetPhysicsAsset())
	{
		UPhysicsAsset* Loaded = FPhysicsAssetFile::LoadForMesh(*Mesh);
		if (!Loaded && bGenerateIfMissing)
			Loaded = FPhysicsAssetUtils::CreateFromSkeletalMesh(Mesh);
		Mesh->SetPhysicsAsset(Loaded);
	}
	return Mesh->GetPhysicsAsset();
}

float USkeletalMeshComponent::GetPhysicsAssetScale() const
{
	const FVector WorldScale = GetWorldScale3D().GetAbs();
	return std::max({ WorldScale.X, WorldScale.Y, WorldScale.Z });
}

bool USkeletalMeshComponent::ShouldCreatePhysicsState() const
{
	if (!GetSkeletalMesh()) return false;
	if (Super::ShouldCreatePhysicsState()) return true;

	// 컴포넌트가 물리를 요청하지 않아도 명시적으로 구동 방식을 지정한 바디가 있을 수 있다.
	if (const UPhysicsAsset* Asset = GetOrCreatePhysicsAsset(false))
	{
		for (const UBodySetup* Setup : Asset->BodySetups)
			if (Setup && Setup->PhysicsType != EPhysicsType::Default) return true;
	}
	return false;
}

void USkeletalMeshComponent::OnCreatePhysicsState()
{
	// UPrimitiveComponent의 바디 하나는 만들지 않는다. 뼈마다 바디를 만든다. (UE도 USceneComponent 쪽으로 건너뛴다)
	USceneComponent::OnCreatePhysicsState();

	if (UPhysicsAsset* PhysicsAsset = GetOrCreatePhysicsAsset())
	{
		InitArticulated(GetWorld()->GetPhysicsScene(), PhysicsAsset);
	}

	// 시뮬레이션 중이면 물리 결과를 뼈로 가져오는 틱을 붙인다. 물리 틱보다 뒤인 PostPhysics 그룹에서 돈다.
	if (bRagdollSimulating)
	{
		EndPhysicsTickFunction.Target = this;
		EndPhysicsTickFunction.bCanEverTick = true;
		EndPhysicsTickFunction.TickGroup = ETickingGroup::PostPhysics;
		EndPhysicsTickFunction.RegisterTickFunction(GetWorld()->GetTickTaskManager());
	}
}

void USkeletalMeshComponent::OnDestroyPhysicsState()
{
	EndPhysicsTickFunction.UnRegisterTickFunction();
	TermArticulated();
	Super::OnDestroyPhysicsState();
}

void FSkeletalMeshComponentEndPhysicsTickFunction::ExecuteTick(float DeltaTime)
{
	// RefreshBoneTransforms 안에서 BlendPhysicsBones가 돌고, 그 결과로 스키닝까지 갱신한다.
	if (Target)
	{
		Target->MarkBoneTransformsDirty();
		Target->UpdateBoneTransformsNow();
	}
}

void USkeletalMeshComponent::InitArticulated(FPhysicsScene* PhysicsScene, UPhysicsAsset* PhysicsAsset)
{
	// 바디를 지금 포즈 자리에 만든다. BeginPlay 시점엔 뼈 행렬이 아직 안 만들어졌을 수 있다.
	RefreshBoneTransformsNow();

	const float Scale = GetPhysicsAssetScale();

	for (UBodySetup* Setup : PhysicsAsset->BodySetups)
	{
		if (!Setup)
		{
			continue;
		}

		const int32 BoneIndex = FindBoneIndex(Setup->BoneName);
		if (BoneIndex < 0)
		{
			continue;
		}

		// 에셋의 기본 설정을 복사해 이 컴포넌트 전용 바디를 만든다 (UE와 같은 DefaultInstance 방식)
		FBodyInstance* Body = new FBodyInstance(Setup->DefaultInstance);
		Body->InstanceBoneIndex = BoneIndex;

		Body->bSimulatePhysics = Setup->PhysicsType == EPhysicsType::Simulated
			|| (Setup->PhysicsType == EPhysicsType::Default && BodyInstance.bSimulatePhysics);
		Body->bEnablePhysicsCollision = true;
		Body->SelfCollisionGroup = GetUUID();   // 겹쳐 있는 뼈 캡슐끼리는 부딪히지 않게 한다
		Body->bNotifyRigidBodyCollision |= BodyInstance.bNotifyRigidBodyCollision;   // 컴포넌트에서 켜면 뼈 바디 전체에 적용
		Body->PhysMaterialOverride = Setup->PhysMaterial;   // DefaultInstance에도 이어져 있지만, 코드로 만든 에셋도 확실히 쓰도록

		Body->InitBody(this, Setup->AggGeom.GetScaled(FVector(Scale, Scale, Scale)), MatrixToBodyTransform(GetBoneWorldMatrix(BoneIndex)), PhysicsScene);
		if (Body->IsValidBodyInstance())
		{
			Bodies.Add(Body);
			if (Body->IsSimulating())
				bRagdollSimulating = true;
			else
				bHasKinematicBodies = true;
		}
		else
		{
			delete Body;
		}
	}

	// 관절. 양쪽 뼈에 바디가 있어야 만든다.
	auto FindBody = [this](const FString& BoneName) -> FBodyInstance*
	{
		const int32 BoneIndex = FindBoneIndex(BoneName);
		for (FBodyInstance* Body : Bodies)
		{
			if (Body->InstanceBoneIndex == BoneIndex)
			{
				return Body;
			}
		}
		return nullptr;
	};

	for (const FConstraintSetup& Setup : PhysicsAsset->ConstraintSetups)
	{
		FBodyInstance* ChildBody  = FindBody(Setup.ChildBoneName);
		FBodyInstance* ParentBody = FindBody(Setup.ParentBoneName);
		if (!ChildBody || !ParentBody)
		{
			continue;
		}

		FConstraintInstance* Constraint = new FConstraintInstance();
		Constraint->InitConstraint(Setup, ChildBody, ParentBody, Scale);
		if (Constraint->IsValidConstraintInstance())
		{
			Constraints.Add(Constraint);
		}
		else
		{
			delete Constraint;
		}
	}
}

void USkeletalMeshComponent::TermArticulated()
{
	// 관절이 바디를 참조하므로 먼저 지운다.
	for (FConstraintInstance* Constraint : Constraints)
	{
		delete Constraint;
	}
	Constraints.Empty();

	for (FBodyInstance* Body : Bodies)
	{
		Body->TermBody();
		delete Body;
	}
	Bodies.Empty();

	bRagdollSimulating = false;
	bHasKinematicBodies = false;
}

void USkeletalMeshComponent::UpdateKinematicBonesToAnim()
{
	for (FBodyInstance* Body : Bodies)
	{
		if (!Body->IsSimulating())
			Body->SetBodyTransform(MatrixToBodyTransform(GetBoneWorldMatrix(Body->InstanceBoneIndex)));
	}
}

void USkeletalMeshComponent::BlendPhysicsBones()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (ComponentSpaceTransforms.Num() != Bones.Num())
	{
		return;
	}

	TArray<FBodyInstance*> BoneBodies;
	BoneBodies.Init(nullptr, Bones.Num());
	for (FBodyInstance* Body : Bodies)
	{
		if (Bones.IsValidIndex(Body->InstanceBoneIndex))
		{
			BoneBodies[Body->InstanceBoneIndex] = Body;
		}
	}

	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix WorldToComponent = ComponentToWorld.Inverse();

	// 부모가 항상 자식보다 앞에 있으므로 한 번 훑으면 부모 결과가 자식에 이어진다.
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (FBodyInstance* Body = BoneBodies[i])
		{
			// 키네마틱 자식은 부모가 시뮬레이션돼도 애니메이션의 컴포넌트 공간 포즈를 유지한다.
			if (!Body->IsSimulating()) continue;
			// 바디 포즈에는 스케일이 없다. 애니메이션 포즈의 뼈 스케일(행 길이)을 다시 붙인다.
			const FMatrix AnimWorld = ComponentSpaceTransforms[i] * ComponentToWorld;
			const FVector BoneScale(
				FVector(AnimWorld.M[0][0], AnimWorld.M[0][1], AnimWorld.M[0][2]).Length(),
				FVector(AnimWorld.M[1][0], AnimWorld.M[1][1], AnimWorld.M[1][2]).Length(),
				FVector(AnimWorld.M[2][0], AnimWorld.M[2][1], AnimWorld.M[2][2]).Length());

			FTransform BodyWorld = Body->GetUnrealWorldTransform();
			BodyWorld.Scale = BoneScale;
			ComponentSpaceTransforms[i] = BodyWorld.GetLocalMatrix() * WorldToComponent;
		}
		else if (Bones[i].ParentIndex >= 0)
		{
			ComponentSpaceTransforms[i] = BoneSpaceTransforms[i].GetLocalMatrix() * ComponentSpaceTransforms[Bones[i].ParentIndex];
		}
	}
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	if (AnimInstance == InInstance)
	{
		return;
	}
	if (AnimInstance)
	{
		AnimInstance->UninitializeAnimation();
	}
    AnimInstance = InInstance;

    if (ManagedAnimGraph && ManagedAnimGraph.get() != InInstance)
    {
        ManagedAnimGraph.reset();
    }

    AnimBlueprintPath.clear();
    bBlueprintReloadRequested = false;

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();
	}
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

    if (!AnimBlueprintPath.empty() && (bBlueprintReloadRequested || !ManagedAnimGraph))
    {
        ReloadAnimBlueprint();
    }

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();

		if (auto* Instance = GetSingleNodeInstance())
		{
			if (Instance->GetCurrentAsset())
			{
				Instance->SetPlaying(true);
			}
		}
	}
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    if (bBlueprintReloadRequested)
    {
        bBlueprintReloadRequested = false;
        ReloadAnimBlueprint();
    }

	// 혼합 래그돌에서는 키네마틱 바디에 보낼 애니메이션을 계속 평가한다.
	if (AnimInstance && GetSkeletalMesh() && (!bRagdollSimulating || bHasKinematicBodies))
	{
        bUpdatingAnimation = true;
		if (auto* Instance = Cast<UAnimSingleNodeInstance>(AnimInstance))
		{
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}

		AnimInstance->UpdateAnimation(DeltaTime);
		FPoseContext Pose(AnimInstance, GetBones());
		AnimInstance->EvaluateAnimation(Pose);
		SetBoneSpaceTransforms(Pose.Pose);
		AnimInstance->TriggerAnimNotifies(DeltaTime);

		// 루트 모션: AnimInstance에 모인 delta를 월드 공간으로 바꿔 CharacterMovement가 가져갈 때까지 쌓아 둔다
		const FRootMotionMovementParams RootMotion = AnimInstance->ConsumeExtractedRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			const FTransform WorldDelta = ConvertRootMotionToWorld(RootMotion.GetRootMotionTransform());
			PendingRootMotion.Location = (bHasPendingRootMotion ? PendingRootMotion.Location : FVector::ZeroVector) + WorldDelta.Location;
			PendingRootMotion.Rotation = bHasPendingRootMotion ? (WorldDelta.Rotation * PendingRootMotion.Rotation).Normalized() : WorldDelta.Rotation;
			bHasPendingRootMotion = true;
		}
        bUpdatingAnimation = false;
	}

	// 천: 이번 틱의 뼈 갱신(RefreshBoneTransforms) 안에서 한 번 돌게 시간을 넘겨 둔다.
	// 포즈가 그대로여도 천은 계속 움직이므로 뼈 갱신을 매 틱 하게 한다.
	if (IsClothingActive())
	{
		EnsureCPUSkinningForCloth();
		PendingClothDeltaTime = DeltaTime;
		MarkBoneTransformsDirty();
	}

	Super::TickComponent(DeltaTime);

	// 뼈 행렬이 갱신된 뒤에 바디를 옮긴다. PrePhysics 틱이라 이번 프레임 simulate에 반영된다.
	if (bHasKinematicBodies)
	{
		UpdateKinematicBonesToAnim();
	}
}

bool USkeletalMeshComponent::IsClothingActive() const
{
	return ClothingSimulation && ClothSectionIndex >= 0 && GetSkeletalMesh();
}

bool USkeletalMeshComponent::RequiresCPUSkinning() const
{
	return IsClothingActive();
}

void USkeletalMeshComponent::EnsureCPUSkinningForCloth()
{
	// GPU 스킨 오브젝트(본 행렬 버퍼가 있음)면 CPU 스킨으로 바꾼다. 천 정점을 CPU에서 덮어써야 하기 때문이다.
	FSkeletalMeshObject* MeshObject = GetMeshObject();
	if (MeshObject && MeshObject->GetBoneMatrixBuffer())
	{
		RefreshSkinningMode();
	}
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return FTransform();
	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return;
	BoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::SetBoneSpaceTransforms(const FCompactPose& Pose)
{
	if (Pose.GetNumBones() != GetBones().Num())
	{
		HTR_LOG(Warning, "SetBoneSpaceTransforms: pose has {} bones, mesh has {}.", Pose.GetNumBones(), GetBones().Num());
		return;
	}
#if defined(_DEBUG)
	for (int32 i = 0; i < Pose.GetNumBones(); ++i)
	{
		const FQuat& Q = Pose[i].Rotation;
		const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (std::abs(LenSq - 1.0f) > 1e-3f)
			HTR_LOG(Warning, "Pose bone {} rotation not normalized (|q|^2 = {})", i, LenSq);
	}
#endif
	BoneSpaceTransforms = Pose.GetBones();
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
	OnSkeletalMeshChanged();
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}

	if (bRagdollSimulating)
	{
		BlendPhysicsBones();
	}

	// 천: 이번 포즈가 정해진 직후, 정점을 올리기(SendRenderDynamicData) 직전에 돈다.
	// 그래서 고정 입자(어깨)가 같은 프레임의 몸과 어긋나지 않는다. 틱 한 번에 한 번만 돈다.
	if (PendingClothDeltaTime > 0.0f)
	{
		const float ClothDeltaTime = PendingClothDeltaTime;
		PendingClothDeltaTime = 0.0f;
		TickClothing(ClothDeltaTime);
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    FArchive& Ar = Record.GetUnderlyingArchive();
    UAnimInstance* SavedInstance = AnimInstance;

    if (Ar.IsSaving() && (!AnimBlueprintPath.empty() || ManagedAnimGraph))
    {
        AnimInstance = nullptr;
    }

    Super::Serialize(Record);

    if (Ar.IsSaving())
    {
        AnimInstance = SavedInstance;
    }

	int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
	FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

	for (int32 I = 0; I < Num; ++I)
	{
		FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
		FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
		FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading())
		{
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index = FindBoneIndex(Name);
			if (Index >= 0)
				BoneSpaceTransforms[Index] = Local;
		}
	}

	SerializeSingleAnimation(Record);

    if (Ar.IsLoading())
    {
        if (!AnimBlueprintPath.empty())
        {
            ReloadAnimBlueprint();
        }

        RefreshBoneTransformsNow();
    }
}

void USkeletalMeshComponent::SerializeSingleAnimation(FStructuredArchive::FRecord& Record)
{
	// 디테일 패널에서 고른 단일 애니메이션(SingleNodeInstance의 에셋)은 AnimInstance 객체가 아니라
	// 에셋 경로로 저장한다. 없으면 필드를 쓰지 않으므로 예전 씬도 그대로 읽힌다.
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimSingleNodeInstance* Instance = GetSingleNodeInstance();
	UAnimationAsset* Asset = Instance ? Instance->GetCurrentAsset() : nullptr;
	const bool bHasAnimation = Ar.IsSaving() && Asset && !Asset->GetAssetPath().empty();

	TOptional<FStructuredArchive::FSlot> AnimSlot = Record.TryEnterField("SingleAnimation", bHasAnimation);
	if (!AnimSlot.IsSet())
		return;

	FStructuredArchive::FRecord AnimRecord = AnimSlot.GetValue().EnterRecord();
	FString Path = Ar.IsSaving() ? Asset->GetAssetPath() : FString();
	bool bLoop = Ar.IsSaving() ? Instance->IsLooping() : true;
	float Rate = Ar.IsSaving() ? Instance->GetPlayRate() : 1.0f;
	AnimRecord << SA_VALUE("Asset", Path) << SA_VALUE("Looping", bLoop) << SA_VALUE("PlayRate", Rate);

	if (!Ar.IsLoading())
		return;

	// AssetManager 캐시로 불러온다 (디테일 패널이 고를 때와 같은 객체)
	UAnimationAsset* Loaded = BlendSpaceFile::IsBlendSpacePath(Path)
		? static_cast<UAnimationAsset*>(UAssetManager::LoadBlendSpace(Path))
		: static_cast<UAnimationAsset*>(UAssetManager::LoadAnimSequence(Path));
	if (!Loaded)
	{
		HTR_LOG(Warning, "SkeletalMeshComponent: single animation not found: {}", Path);
		return;
	}

	// 재생 시작은 BeginPlay가 한다 (에디터 월드에서는 첫 프레임 포즈만 보인다)
	SetAnimation(Loaded);
	if (UAnimSingleNodeInstance* NewInstance = GetSingleNodeInstance())
	{
		NewInstance->SetLooping(bLoop);
		NewInstance->SetPlayRate(Rate);
	}
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
    bBlueprintReloadRequested = !AnimBlueprintPath.empty();
	BoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		BoneSpaceTransforms.Add(Bone.LocalBindPose);
}
UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		HTR_LOG(Warning, "SetAnimation: unsupported animation asset type");
		return;
	}
    AnimBlueprintPath.clear();
    AnimBlueprintError.clear();
    bBlueprintReloadRequested = false;
	auto* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);

	if (!SingleNodeInstance)
	{
		SingleNodeInstance = NewObject<UAnimSingleNodeInstance>(this);
		SetAnimInstance(SingleNodeInstance);
	}

	SingleNodeInstance->SetAnimationAsset(NewAnimToPlay);
	SingleNodeInstance->SetBlendSpaceInput(BlendSpaceInput);

	RefreshAnimationPose();
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetLooping(bLooping);
		Instance->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		return;
	}

	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
    if (!AnimInstance)
    {
        ResetPose();
        RefreshBoneTransformsNow();
        return;
    }

	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}

	FPoseContext Pose(AnimInstance, GetBones());
	AnimInstance->EvaluateAnimation(Pose);
	SetBoneSpaceTransforms(Pose.Pose);
	RefreshBoneTransformsNow();
}


FTransform USkeletalMeshComponent::ConsumeRootMotion()
{
	const FTransform Result = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return Result;
}

FTransform USkeletalMeshComponent::ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const
{
	// delta는 root 본의 부모 공간 값이다. UAL은 Armature → root 구조라 부모(Armature)를 거쳐야 컴포넌트 공간이 된다.
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 RootIndex = GetRootMotionBoneIndex();
	const int32 ParentIndex = RootIndex >= 0 ? Bones[RootIndex].ParentIndex : -1;

	// 이동: 부모(바인드 포즈) → 컴포넌트 → 월드 행렬로 방향·스케일만 적용 (Armature 스케일, 메시 0.01, yaw −180 모두 여기서)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix ParentToWorld = ParentIndex >= 0
		? Bones[ParentIndex].InverseBindPose.Inverse() * ComponentToWorld
		: ComponentToWorld;

	FTransform Result;
	Result.Location = ParentToWorld.TransformVector(ParentSpaceDelta.Location);

	// 회전: 부모 공간 회전을 월드 공간 회전으로 바꾼다 (P × Δ × P⁻¹, P = 컴포넌트 월드 회전 × 부모 본 회전)
	FQuat ParentRotation = FQuat(GetWorldRotation());
	FQuat BoneChainRotation;
	for (int32 Index = ParentIndex; Index >= 0; Index = Bones[Index].ParentIndex)
	{
		BoneChainRotation = Bones[Index].LocalBindPose.Rotation * BoneChainRotation;
	}
	ParentRotation = (ParentRotation * BoneChainRotation).Normalized();
	Result.Rotation = (ParentRotation * ParentSpaceDelta.Rotation * ParentRotation.Inverse()).Normalized();
	return Result;
}

int32 USkeletalMeshComponent::GetRootMotionBoneIndex() const
{
	const int32 RootIndex = FindBoneIndex("root");
	if (RootIndex >= 0)
	{
		return RootIndex;
	}
	return GetBones().IsEmpty() ? -1 : 0;
}

void USkeletalMeshComponent::DrawRootDebug(FLineBatcher* LineBatcher) const
{
	const int32 RootIndex = GetRootMotionBoneIndex();
	if (!LineBatcher || RootIndex < 0 || RootIndex >= ComponentSpaceTransforms.Num())
	{
		return;
	}

	// root 본 위치에 구체
	const FVector Root = GetBoneWorldMatrix(RootIndex).TransformPosition(FVector::ZeroVector);
	const FVector4 Color(1.0f, 0.3f, 0.8f, 1.0f);
	LineBatcher->AddSphere(Root, 0.08f, Color);

	// 액터 위치를 root 높이로 내린 점 → root. 선 길이 = root가 액터(캡슐)에서 벗어난 거리
	if (const AActor* Owner = GetOwner())
	{
		const FVector ActorLocation = Owner->GetActorLocation();
		const FVector Base(ActorLocation.X, ActorLocation.Y, Root.Z);
		LineBatcher->AddLine(Base, Root, Color);
		LineBatcher->AddSphere(Base, 0.03f, Color);
	}
}
bool GShowSkeletalRootDebug = false;

void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered()) continue;
		Mesh->DrawRootDebug(LineBatcher);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	// 등록 해제 없이 바로 지워지는 경우 (에셋 미리보기 등)
	delete ClothingSimulationContext;
	delete ClothingSimulation;

    if (ManagedAnimGraph)
    {
        if (AnimInstance == ManagedAnimGraph.get())
        {
            SetAnimInstance(nullptr);
        }
        else
        {
            ManagedAnimGraph->UninitializeAnimation();
        }
    }
}

const FString& USkeletalMeshComponent::GetAnimBlueprintPath() const
{
    return AnimBlueprintPath;
}

const FString& USkeletalMeshComponent::GetAnimBlueprintError() const
{
    return AnimBlueprintError;
}

bool USkeletalMeshComponent::SetAnimBlueprint(const FString& Path)
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Load or reload Animation Blueprints from actor Tick, outside animation callbacks.";
        return false;
    }

    if (Path.empty())
    {
        const bool bHadBlueprint = !AnimBlueprintPath.empty();
        AnimBlueprintPath.clear();
        AnimBlueprintError.clear();
        bBlueprintReloadRequested = false;

        if (ManagedAnimGraph && bHadBlueprint)
        {
            SetAnimInstance(nullptr);
            ResetPose();
            RefreshBoneTransformsNow();
        }

        return true;
    }

    AnimBlueprintError.clear();
    const FString Key = FAssetUtils::GetAssetKey(Path);
    const FString FilePath = FAssetUtils::ResolveAssetFilePath(Key);
    FAnimBlueprintData Data;

    if (!AnimBlueprintFile::IsAnimBlueprintPath(Key) || !AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? Key : FilePath), Data, AnimBlueprintError))
    {
        if (AnimBlueprintError.empty())
        {
            AnimBlueprintError = "Use an .animblueprint asset.";
        }

        return false;
    }

    TUniquePtr<UAnimGraphInstance> Instance;
    AnimBlueprintCompiler::FNodeMap NodeIndices;

    if (!AnimBlueprintCompiler::Build(Data, *this, Instance, NodeIndices, AnimBlueprintError))
    {
        return false;
    }

    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    AnimBlueprintPath = Key;
    bBlueprintReloadRequested = false;
    AnimBlueprintError.clear();
    RefreshAnimationPose();
    return true;
}

bool USkeletalMeshComponent::ReloadAnimBlueprint()
{
    const FString Path = AnimBlueprintPath;
    bBlueprintReloadRequested = false;

    if (Path.empty())
    {
        return false;
    }

    if (!SetAnimBlueprint(Path))
    {
        HTR_LOG(Warning, "AnimBlueprint load failed: {} ({})", Path, AnimBlueprintError);
        return false;
    }

    return true;
}

UAnimGraphInstance* USkeletalMeshComponent::CreateAnimGraph()
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Create animation graphs outside animation callbacks.";
        return nullptr;
    }

    if (!AnimBlueprintPath.empty())
    {
        AnimBlueprintError = "An Animation Blueprint is assigned. Use GetAnimGraph to update its variables.";
        return nullptr;
    }

    AnimBlueprintError.clear();

    if (UAnimGraphInstance* Existing = Cast<UAnimGraphInstance>(AnimInstance))
    {
        Existing->ResetGraph();
        return Existing;
    }

    TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(this));
    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    return ManagedAnimGraph.get();
}
