#pragma once

#include "SkeletalMeshComponent.h"
#include "Math/Rotator.h"
#include "ClothComponent.generated.h"

class UStaticMesh;
class USkeletalMesh;

// 메시 하나 전체가 천인 컴포넌트 (망토, 깃발, 커튼). 캐릭터 메시에 합치지 않고 따로 붙인다.
//   ClothMesh(스태틱 메시)를 뼈 하나짜리 스켈레탈 메시로 바꿔, 그 전체(섹션 0)를 천으로 시뮬레이션한다.
//   위쪽은 ClothPinHeightRatio만큼 고정된다 (메시 공간 +Z가 위).
//   부모가 스켈레탈 메시 컴포넌트이고 AttachBoneName을 정하면 매 틱 그 뼈를 따라간다. 캐릭터가 움직이면 관성으로 펄럭인다.
// UE 4.27에는 없는 데모용 컴포넌트다. 천 처리(에셋, 시뮬레이션, 렌더 반영)는 USkeletalMeshComponent 것을 그대로 쓴다.
UCLASS()
class UClothComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()
public:
	UClothComponent();

	// 천으로 쓸 메시. 얇은 판 모양이 좋다. 바꾸면 천을 다시 만든다
	UPROPERTY(EditAnywhere)
	UStaticMesh* ClothMesh = nullptr;

	// 따라갈 부모 스켈레탈 메시의 뼈 이름 (예: spine_05). 비우면 평범하게 부모에 붙어 있다
	UPROPERTY(EditAnywhere)
	FString AttachBoneName;

	// 뼈 기준 위치·회전 오프셋. 뼈를 따라갈 때는 컴포넌트 트랜스폼 대신 이 값으로 자리를 맞춘다
	UPROPERTY(EditAnywhere)
	FVector AttachLocationOffset = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere)
	FRotator AttachRotationOffset;

	void SetClothMesh(UStaticMesh* InClothMesh);

	void TickComponent(float DeltaTime) override;
	void PostEditChangeProperty(const FProperty& Property) override;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	void OnRegister() override;

	// 자기 메시는 천뿐이라 Physics Asset을 자동으로 만들면 천을 감싸는 캡슐이 생겨 천이 스스로를 밀어낸다. 충돌체 없음
	UPhysicsAsset* GetClothCollisionPhysicsAsset() const override { return nullptr; }

private:
	// ClothMesh로 뼈 하나짜리 스켈레탈 메시를 만들어(같은 스태틱 메시면 재사용) 이 컴포넌트의 메시로 쓴다
	void RebuildClothSkeletalMesh();

	// 부모 스켈레탈 메시의 AttachBoneName 뼈 자리로 이 컴포넌트를 옮긴다
	void FollowAttachBone();

	// 스태틱 메시 → 뼈(root) 하나, 모든 정점 가중치 1, 섹션 하나인 스켈레탈 메시
	static USkeletalMesh* CreateSkeletalMeshFromStaticMesh(UStaticMesh* StaticMesh);

	// 지금 스켈레탈 메시를 만든 원본. 디테일 패널은 ClothMesh를 직접 바꾸므로 이 값과 비교해 바뀐 걸 알아챈다
	UStaticMesh* BuiltFromMesh = nullptr;
};
