#pragma once

#include "UObject/Object.h"
#include "Containers/Array.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "PhysicsAsset.generated.h"

class USkeletalMesh;

// 본 하나에 대응하는 물리 바디 정의 (언리얼 USkeletalBodySetup 축소판)
// Shape의 크기·오프셋은 본 로컬 공간 기준이며, 실제 월드 스케일은 11단계에서 바디 생성 시 반영한다.
struct FSkeletalBodySetup
{
	FString BoneName;
	FBodyShapeDesc Shape;

	float MassInKg = 1.0f;
	float LinearDamping = 0.2f;
	float AngularDamping = 1.5f;
};

// 두 본(자식-부모)을 연결하는 조인트 정의 (언리얼 UPhysicsConstraintTemplate 축소판)
// 언리얼 규약: ConstraintBone1 = 자식 본, ConstraintBone2 = 부모 본
struct FPhysicsConstraintSetup
{
	FString JointName;
	FString ConstraintBone1;
	FString ConstraintBone2;

	EAngularConstraintMotion Swing1Motion = EAngularConstraintMotion::ACM_Limited;
	EAngularConstraintMotion Swing2Motion = EAngularConstraintMotion::ACM_Limited;
	EAngularConstraintMotion TwistMotion = EAngularConstraintMotion::ACM_Limited;

	float Swing1LimitDegrees = 20.0f;
	float Swing2LimitDegrees = 15.0f;
	float TwistLimitDegrees = 10.0f;

	float AngularDamping = 5.0f;
	float AngularStiffness = 0.0f;
	bool bDisableCollision = true;
};

// 스켈레탈 메시의 래그돌/부위별 충돌용 바디·조인트 데이터 에셋 (언리얼 UPhysicsAsset 대응)
UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()
public:
	UPhysicsAsset() = default;
	virtual ~UPhysicsAsset() override = default;

	// 본 이름으로 바디/조인트 인덱스를 찾는다 (없으면 -1 반환).
	int32 FindBodyIndex(const FString& BoneName) const;
	int32 FindConstraintIndex(const FString& Bone1Name, const FString& Bone2Name) const;

	void Clear();

	// USkeletalMesh의 스키닝 바운드와 본 계층을 분석해 바디(캡슐/구)와 부모-자식 조인트를 자동 생성한다.
	// MinBoneSize(m 단위)보다 작은 본(손가락·리프 등)은 자동으로 제외된다.
	void GenerateFromSkeletalMesh(const USkeletalMesh* Mesh, float MinBoneSize = 0.05f);

	// PhysicsAsset 정의를 JSON 파일로 저장/불러오기 한다.
	bool SaveToFile(const FString& FilePath, FString& OutError) const;
	bool LoadFromFile(const FString& FilePath, FString& OutError);

public:
	TArray<FSkeletalBodySetup> SkeletalBodySetups;
	TArray<FPhysicsConstraintSetup> ConstraintSetups;
};