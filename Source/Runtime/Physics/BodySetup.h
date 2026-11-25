#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Physics/AggregateGeom.h"
#include "Physics/BodyInstance.h"
#include "BodySetup.generated.h"

class UPhysicalMaterial;

UENUM()
enum class EPhysicsType : uint8
{
	Default,
	Kinematic,
	Simulated,
};

// 바디 하나의 설계도: 어느 뼈에, 어떤 도형을, 어떤 설정으로 붙일지. UE 4.27의 UBodySetup 자리다.
// 에셋 데이터라서 PhysX 객체는 없다. 런타임에 DefaultInstance를 복사해 FBodyInstance를 만들고 InitBody에 AggGeom을 넘긴다.
UCLASS()
class UBodySetup : public UObject
{
	GENERATED_BODY()
public:
	UBodySetup() = default;
	virtual ~UBodySetup();

	// 이 바디가 붙는 뼈. 스태틱 메시용이면 비어 있다.
	UPROPERTY(EditAnywhere)
	FString BoneName;

	// 스켈레탈 바디의 구동 방식. 기존 에셋은 Default로 컴포넌트 설정을 따른다.
	UPROPERTY(EditAnywhere)
	EPhysicsType PhysicsType = EPhysicsType::Default;

	// 런타임 바디가 복사해 갈 기본 설정 (질량, 중력 등)
	UPROPERTY(EditAnywhere)
	FBodyInstance DefaultInstance;

	// 도형 목록. 위치·크기는 뼈 로컬, 메시 원본 단위 (컴포넌트 스케일은 런타임에 곱한다).
	// 리플렉션이 배열을 아직 지원하지 않아 UPROPERTY가 아니다. Serialize에서 직접 저장한다.
	FKAggregateGeom AggGeom;

	// 이 바디의 표면 재질. nullptr이면 엔진 기본 재질. 이 UBodySetup이 소유한다.
	// UE는 에셋 참조(UPhysicalMaterial*)지만, 재질 에셋 파일이 아직 없어서 바디 설계도 안에 값으로 함께 저장한다.
	UPhysicalMaterial* PhysMaterial = nullptr;

	// 없으면 만들어서 돌려준다 (에디터에서 재질을 처음 고칠 때).
	UPhysicalMaterial* GetOrCreatePhysMaterial();
	void ClearPhysMaterial();
	// UPhysicalMaterial 소멸자가 부른다 (엔진 종료 때 일괄 삭제에서 재질이 먼저 지워지는 경우).
	void OnPhysMaterialDestroyed(UPhysicalMaterial* Material);

	// UPROPERTY(BoneName, DefaultInstance)는 리플렉션이, AggGeom과 PhysMaterial은 여기서 직접 저장한다.
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
};
