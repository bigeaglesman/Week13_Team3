#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Containers/Array.h"
#include "Physics/ConstraintSetup.h"
#include "PhysicsAsset.generated.h"

class UBodySetup;

// 래그돌 하나의 설계도: 뼈마다의 바디(UBodySetup)와 바디 사이의 관절(FConstraintSetup).
// UE 4.27의 UPhysicsAsset 자리다. USkeletalMesh가 갖고, 에디터(Physics Asset Editor)가 이 데이터를 고친다.
UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()
public:
	UPhysicsAsset() = default;
	virtual ~UPhysicsAsset();

	// 뼈 이름으로 바디 찾기. 없으면 -1
	int32 FindBodyIndex(const FString& BoneName) const;

	// 뼈 하나당 바디 최대 하나. 이 에셋이 소유하고 소멸자에서 지운다.
	TArray<UBodySetup*> BodySetups;

	// 리플렉션이 배열을 아직 지원하지 않아 UPROPERTY가 아니다.
	TArray<FConstraintSetup> ConstraintSetups;
};
