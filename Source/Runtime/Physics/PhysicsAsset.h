#pragma once
#include "UObject/Object.h"
#include "PhysicsAsset.generated.h"

class UBodySetup;
class USkeletalMesh;

// UE UPhysicsAsset. 스켈레탈 메시의 본마다 붙는 바디(UBodySetup) 묶음
UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()
public:
	~UPhysicsAsset() override;

	// 본마다 붙은 정점을 감싸는 캡슐을 만든다. UE PhysicsAssetUtils::CreateFromSkeletalMesh의 단순화
	void CreateFromSkeletalMesh(const USkeletalMesh& Mesh);

	int32 FindBodyIndex(const FName& BoneName) const;

	// UObject 일괄 정리에서 BodySetup이 먼저 지워지면 목록에서 뺀다
	void OnBodySetupDestroyed(UBodySetup* BodySetup);

	TArray<UBodySetup*> SkeletalBodySetups;
};
