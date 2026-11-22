#pragma once

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Containers/Array.h"

// 바디 하나를 이루는 충돌 도형들. 위치와 회전은 바디(컴포넌트) 기준 로컬이고, 크기에는 스케일이 이미 곱해져 있다.
// UE 4.27의 FKAggregateGeom / FKSphereElem / FKBoxElem / FKSphylElem 자리다.
// 2단계에서는 Shape 컴포넌트가 매번 채우고, 3단계에서 UBodySetup이 이걸 에셋으로 들고 간다.

struct FKSphereElem
{
	FVector Center = FVector(0, 0, 0);
	float   Radius = 0.5f;
};

struct FKBoxElem
{
	FVector Center   = FVector(0, 0, 0);
	FQuat   Rotation = FQuat::Identity();
	FVector HalfExtent = FVector(0.5f, 0.5f, 0.5f);   // UE는 전체 길이(X, Y, Z)를 저장한다. 여기선 PhysX와 같은 반 크기
};

// Sphyl = 캡슐. 축은 로컬 Z (엔진 UCapsuleComponent와 같음).
struct FKSphylElem
{
	FVector Center   = FVector(0, 0, 0);
	FQuat   Rotation = FQuat::Identity();
	float   Radius = 0.5f;
	float   Length = 1.0f;   // 반구를 뺀 원기둥 부분의 전체 길이 (UE와 같음)
};

struct FKAggregateGeom
{
	TArray<FKSphereElem> SphereElems;
	TArray<FKBoxElem>    BoxElems;
	TArray<FKSphylElem>  SphylElems;

	bool IsEmpty() const { return SphereElems.IsEmpty() && BoxElems.IsEmpty() && SphylElems.IsEmpty(); }
};
