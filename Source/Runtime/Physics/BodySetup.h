#pragma once
#include "UObject/Object.h"
#include "BodySetup.generated.h"

class UStaticMesh;
class UWorld;
class FLineBatcher;

// UE FKShapeElem 계열. 모든 값은 바디 원점 기준 로컬 공간, 미터 단위다.
struct FKSphereElem
{
	FVector Center = FVector(0.0f, 0.0f, 0.0f);
	float Radius = 0.5f;
};

struct FKBoxElem
{
	FVector Center = FVector(0.0f, 0.0f, 0.0f);
	FRotator Rotation;
	float X = 1.0f;   // UE와 같이 전체 길이(half extent가 아니다)
	float Y = 1.0f;
	float Z = 1.0f;
};

// 캡슐. UE와 같이 로컬 Z축 방향으로 눕지 않고 서 있다. Length는 반구를 뺀 원통 길이다.
struct FKSphylElem
{
	FVector Center = FVector(0.0f, 0.0f, 0.0f);
	FRotator Rotation;
	float Radius = 0.25f;
	float Length = 1.0f;
};

// 바디 하나를 이루는 도형 묶음. PhysX에서는 액터 하나에 붙는 PxShape 여러 개가 된다.
struct FKAggregateGeom
{
	TArray<FKSphereElem> SphereElems;
	TArray<FKBoxElem> BoxElems;
	TArray<FKSphylElem> SphylElems;

	int32 GetElementCount() const { return SphereElems.Num() + BoxElems.Num() + SphylElems.Num(); }

	// 월드 공간 와이어프레임. 스케일은 PhysX 도형과 같은 규칙으로 곱한다
	void DrawDebug(FLineBatcher& LineBatcher, const FVector& Location, const FQuat& Rotation, const FVector& Scale, const FVector4& Color) const;
};

// Collision 쇼 플래그. 바디를 만드는 컴포넌트의 BodySetup을 그린다
void DrawPhysicsCollision(UWorld* World, FLineBatcher* LineBatcher);

// UE UBodySetup. 바디 하나의 충돌 모양 에셋 데이터. 런타임 바디(FBodyInstance)는 이걸 읽어서 만든다.
UCLASS()
class UBodySetup : public UObject
{
	GENERATED_BODY()
public:
	~UBodySetup() override;

	FName BoneName;   // 스켈레탈 메시에서 이 바디가 붙는 본. 스태틱 메시는 비워 둔다
	FKAggregateGeom AggGeom;

private:
	friend class UStaticMesh;
	UStaticMesh* OwningStaticMesh = nullptr;
};
