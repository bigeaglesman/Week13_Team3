#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Containers/Array.h"
#include "Physics/ConstraintSetup.h"
#include "PhysicsAsset.generated.h"

class UBodySetup;
class USkeletalMesh;

// 래그돌 하나의 설계도: 뼈마다의 바디(UBodySetup)와 바디 사이의 관절(FConstraintSetup).
// UE 4.27의 UPhysicsAsset 자리다. USkeletalMesh가 갖고, 에디터(Physics Asset Editor)가 이 데이터를 고친다.
// 파일 저장·로드는 PhysicsAssetFile.h (.physicsasset, JSON)
UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()
public:
	UPhysicsAsset() = default;
	virtual ~UPhysicsAsset();

	// 뼈 이름으로 바디 찾기. 없으면 -1
	int32 FindBodyIndex(const FString& BoneName) const;
	bool HasConstraintBetween(const FString& BoneA, const FString& BoneB) const;
	bool RemoveBody(const FString& BoneName);

	// 바디와 관절을 모두 지운다.
	void Clear();

	// UBodySetup 소멸자가 부른다 (엔진 종료 때 일괄 삭제에서 바디가 먼저 지워지는 경우).
	void OnBodySetupDestroyed(UBodySetup* Setup);

	// 이 에셋을 가진 메시. USkeletalMesh::SetPhysicsAsset이 정한다. 에셋이 먼저 지워지면 메시의 포인터를 비운다.
	USkeletalMesh* OwningMesh = nullptr;

	// 바디·관절을 통째로 저장·로드한다. 로드하면 기존 바디는 지우고 새로 만든다.
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// 뼈 하나당 바디 최대 하나. 이 에셋이 소유하고 소멸자에서 지운다.
	TArray<UBodySetup*> BodySetups;

	// 리플렉션이 배열을 아직 지원하지 않아 UPROPERTY가 아니다.
	TArray<FConstraintSetup> ConstraintSetups;

	// 이 에셋을 읽어 온 파일 (.physicsasset). 자동 생성했거나 아직 저장 전이면 비어 있다.
	FString AssetPath;

	// 파일에 쓰지 않는 편집 상태. 메시 에디터 탭을 다시 열어도 공유 에셋에 남는다.
	bool bHasUnsavedChanges = false;
};
