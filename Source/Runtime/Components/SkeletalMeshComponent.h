#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSequence;
class UBlendSpace1D;
class UWorld;
class FLineBatcher;
class FPhysicsScene;
class UPhysicsAsset;
class FConstraintInstance;
class USkeletalMeshComponent;

// 물리 이후에 도는 두 번째 틱. 래그돌이 시뮬레이션 중일 때만 등록해서 물리 결과를 뼈에 반영한다.
// UE의 FSkeletalMeshComponentEndPhysicsTickFunction 자리다.
struct FSkeletalMeshComponentEndPhysicsTickFunction : public FTickFunction
{
	USkeletalMeshComponent* Target = nullptr;
	void ExecuteTick(float DeltaTime) override;
};

// World의 SkeletalMeshComponent마다 root 디버그를 그린다 (DrawCollisionShapes와 같은 방식). PIE의 Show Root에서 호출
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher);
// Settings > Debug Draw (PIE) > Show Root 값. 에디터가 매 프레임 갱신하고, 런타임 액터("root" 라벨 등)가 읽는다
extern bool GShowSkeletalRootDebug;
class UAnimationAsset;
class UAnimSingleNodeInstance;
class UAnimGraphInstance;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
    USkeletalMeshComponent();
    ~USkeletalMeshComponent() override;
    bool SetAnimBlueprint(const FString& Path);
    bool ReloadAnimBlueprint();
    UAnimGraphInstance* CreateAnimGraph();
    const FString& GetAnimBlueprintPath() const;
    const FString& GetAnimBlueprintError() const;
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	void SetBoneSpaceTransforms(const struct FCompactPose& Pose); // 애니메이션 평가 결과를 받는 입구
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

	void ResetPose();
	void RefreshBoneTransformsNow();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetAnimation(UAnimationAsset* NewAnimToPlay);
	void Play(bool bLooping = true);
	void Stop();
	bool IsPlaying() const;
	void PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLoop = true);
	void RefreshAnimationPose();

	UPROPERTY()
	float BlendSpaceInput = 300.0f;

	// 루트 모션: 이번 Tick까지 모인 이동량(월드 공간). CharacterMovement가 가져가 Actor를 움직인다
	bool HasPendingRootMotion() const { return bHasPendingRootMotion; }
	FTransform ConsumeRootMotion();

	// root 부모 공간 delta → 월드 공간 delta (Unreal ConvertLocalRootMotionToWorld + Armature 변환). 시퀀스 뷰어 미리보기도 쓴다
	FTransform ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const;

	// 루트 모션이 뽑히는 본. "root"를 먼저, 없으면 첫 본 (UAnimSequence::FindRootTrack과 같은 규칙)
	int32 GetRootMotionBoneIndex() const;

	// 디버그: root 본 구체 + 액터 위치(root 높이)에서 root까지의 선
	void DrawRootDebug(FLineBatcher* LineBatcher) const;

	// ---- 물리 (래그돌) ----
	const TArray<FBodyInstance*>& GetBodies() const { return Bodies; }
	int32 GetNumConstraints() const { return Constraints.Num(); }

	// 메시의 Physics Asset. 아직 없으면 메시 옆의 .physicsasset을 읽거나, 없으면 뼈 모양으로 자동 생성해 메시에 붙인다.
	// 메시가 없거나 뼈가 없으면 nullptr
	UPhysicsAsset* GetOrCreatePhysicsAsset() const;

	// Physics Asset 도형(메시 원본 단위)에 곱할 배율. 캡슐·구는 비균일 스케일을 못 하므로 가장 큰 축
	float GetPhysicsAssetScale() const;

	// BodyInstance.bSimulatePhysics가 켜진 채로 바디가 만들어졌으면 true. 이때는 애니메이션 대신 물리가 뼈를 움직인다.
	bool IsRagdollSimulating() const { return bRagdollSimulating; }

	// 물리 결과를 뼈에 반영한다. 바디가 있는 뼈는 바디 포즈를 쓰고, 없는 뼈는 마지막 로컬 포즈 그대로 부모를 따라간다.
	// ComponentSpaceTransforms를 덮어쓰므로 RefreshBoneTransforms 안에서 부른다. UE의 BlendPhysicsBones
	void BlendPhysicsBones();

protected:
	// BodyInstance의 bEnablePhysicsCollision 또는 bSimulatePhysics가 켜져 있으면 바디 하나 대신 Physics Asset대로 뼈마다 바디를 만든다.
	// 메시에 Physics Asset이 없으면 뼈 모양으로 자동 생성해 붙인다.
	virtual bool ShouldCreatePhysicsState() const override;
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;

	// UE의 InitArticulated / TermArticulated
	void InitArticulated(FPhysicsScene* PhysicsScene, UPhysicsAsset* PhysicsAsset);
	void TermArticulated();

	// 뼈 바디를 지금 애니메이션 포즈로 옮긴다 (키네마틱). UE의 UpdateKinematicBonesToAnim
	void UpdateKinematicBonesToAnim();

	// 단일 애니메이션 에셋 경로/루프/재생 속도를 씬에 저장·복원한다 (Serialize에서 부른다)
	void SerializeSingleAnimation(FStructuredArchive::FRecord& Record);

	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

private:
    UPROPERTY()
    FString AnimBlueprintPath;

    TUniquePtr<UAnimGraphInstance> ManagedAnimGraph;
    FString AnimBlueprintError;
    bool bBlueprintReloadRequested = false;
    bool bUpdatingAnimation = false;

	UPROPERTY()
	UAnimInstance* AnimInstance = nullptr;

	TArray<FTransform> BoneSpaceTransforms;

	FTransform PendingRootMotion;           // 월드 공간 누적값
	bool bHasPendingRootMotion = false;

	// 뼈마다의 런타임 바디. Physics Asset의 BodySetups 순서와 다를 수 있다 (뼈를 못 찾은 바디는 빠진다). UE의 Bodies
	TArray<FBodyInstance*> Bodies;

	// 바디 사이의 런타임 관절. UE의 Constraints
	TArray<FConstraintInstance*> Constraints;

	FSkeletalMeshComponentEndPhysicsTickFunction EndPhysicsTickFunction;
	bool bRagdollSimulating = false;
};
