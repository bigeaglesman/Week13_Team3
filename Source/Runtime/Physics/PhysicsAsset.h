#pragma once

#include "UObject/Object.h"
#include "Math/Transform.h"
#include "PhysicsAsset.generated.h"

class USkeletalMesh;

enum class EPhysicsShapeType : uint8
{
    Box,
    Sphere,
    Capsule
};

enum class EPhysicsConstraintMotion : uint8
{
    Locked,
    Limited,
    Free
};

struct FPhysicsShapeSetup
{
    EPhysicsShapeType ShapeType = EPhysicsShapeType::Box;

    // 본 기준 로컬 위치와 회전
    FTransform LocalPose;

    // Box: XYZ 반쪽 크기
    // Sphere: X 반지름
    // Capsule: X 반지름, Z 반구 중심까지의 반높이
    FVector Dimensions = FVector(0.5f, 0.5f, 0.5f);

    float StaticFriction = 0.5f;
    float DynamicFriction = 0.5f;
    float Restitution = 0.0f;
};

struct FPhysicsBodySetup
{
    FString BoneName;
    TArray<FPhysicsShapeSetup> Shapes;

    float Mass = 1.0f;
    float LinearDamping = 0.01f;
    float AngularDamping = 0.05f;

    bool bEnableGravity = true;
    bool bStartAwake = true;
    bool bKinematic = false;
};

struct FPhysicsConstraintSetup
{
    FString BoneNameA;
    FString BoneNameB;

    FTransform FrameA;
    FTransform FrameB;

    EPhysicsConstraintMotion LinearX = EPhysicsConstraintMotion::Locked;
    EPhysicsConstraintMotion LinearY = EPhysicsConstraintMotion::Locked;
    EPhysicsConstraintMotion LinearZ = EPhysicsConstraintMotion::Locked;

    float LinearLimit = 0.1f;

    EPhysicsConstraintMotion Swing1 = EPhysicsConstraintMotion::Limited;
    EPhysicsConstraintMotion Swing2 = EPhysicsConstraintMotion::Limited;
    EPhysicsConstraintMotion Twist = EPhysicsConstraintMotion::Limited;

    float Swing1Limit = 45.0f;
    float Swing2Limit = 45.0f;
    float TwistLimit = 45.0f;
};

UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY();
public:
    const FString& GetSkeletalMeshPath() const { return SkeletalMeshPath; }
    void SetSkeletalMeshPath(FString InPath) { SkeletalMeshPath = std::move(InPath); }

    const FString& GetAssetPath() const { return AssetPath; }
    void SetAssetPath(FString InPath) { AssetPath = std::move(InPath); }

    const TArray<FPhysicsBodySetup>& GetBodySetups() const { return BodySetups; }
    TArray<FPhysicsBodySetup>& GetBodySetups() { return BodySetups; }

    const TArray<FPhysicsConstraintSetup>& GetConstraintSetups() const { return ConstraintSetups; }
    TArray<FPhysicsConstraintSetup>& GetConstraintSetups() { return ConstraintSetups; }

    bool ValidateAgainstSkeletalMesh(const USkeletalMesh* SkeletalMesh, FString& OutError) const;

    using Super::Serialize;
    void Serialize(FStructuredArchive::FRecord Record) override;
private:
	FString SkeletalMeshPath;
	FString AssetPath;

    TArray<FPhysicsBodySetup> BodySetups;
    TArray<FPhysicsConstraintSetup> ConstraintSetups;
};