#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Physics/AggregateGeom.h"
#include "Physics/BodyInstance.h"
#include "BodySetup.generated.h"

// 바디 하나의 설계도: 어느 뼈에, 어떤 도형을, 어떤 설정으로 붙일지. UE 4.27의 UBodySetup 자리다.
// 에셋 데이터라서 PhysX 객체는 없다. 런타임에 DefaultInstance를 복사해 FBodyInstance를 만들고 InitBody에 AggGeom을 넘긴다.
UCLASS()
class UBodySetup : public UObject
{
	GENERATED_BODY()
public:
	// 이 바디가 붙는 뼈. 스태틱 메시용이면 비어 있다.
	UPROPERTY(EditAnywhere)
	FString BoneName;

	// 런타임 바디가 복사해 갈 기본 설정 (질량, 중력 등)
	UPROPERTY(EditAnywhere)
	FBodyInstance DefaultInstance;

	// 도형 목록. 위치·크기는 뼈 로컬, 메시 원본 단위 (컴포넌트 스케일은 런타임에 곱한다).
	// 리플렉션이 배열을 아직 지원하지 않아 UPROPERTY가 아니다.
	FKAggregateGeom AggGeom;
};
