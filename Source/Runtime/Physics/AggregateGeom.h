#pragma once

#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Quat.h"
#include "Containers/Array.h"

class FLineBatcher;
struct FRay;

// 바디 하나를 이루는 충돌 도형들. 위치와 회전은 바디(컴포넌트) 기준 로컬이고, 크기에는 스케일이 이미 곱해져 있다.
// UE 4.27의 FKAggregateGeom / FKSphereElem / FKBoxElem / FKSphylElem 자리다.
// Shape 컴포넌트가 매번 채우고, UBodySetup이 이걸 에셋으로 들고 간다.

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

// 볼록 껍질. 점들을 넘기면 PhysX 쿠킹이 껍질을 계산한다 (최대 255 정점). 시뮬레이션하는 바디에도 쓸 수 있다.
// 점 위치는 바디 로컬이고 스케일이 이미 곱해져 있다.
struct FKConvexElem
{
	TArray<FVector> VertexData;

	// 디버그 와이어프레임용 껍질 모서리 (VertexData 인덱스 2개씩). 쿠킹으로 껍질을 계산했을 때만 채워진다. 저장하지 않는다.
	TArray<uint32> DebugEdges;
};

// 삼각형 메시 그대로의 충돌 (UE의 Complex Collision). PhysX는 시뮬레이션하는 바디에 이 도형을 허용하지 않아서
// 바닥·벽처럼 움직이지 않거나 키네마틱인 바디에만 붙는다.
// UE는 이 데이터를 FKAggregateGeom이 아니라 UBodySetup::TriMeshes(쿠킹 결과)로 갖는다. 여기선 런타임 도형을 한곳에 모으려고 같이 둔다.
struct FKTriMeshElem
{
	TArray<FVector> Vertices;
	TArray<uint32>  Indices;   // 삼각형마다 3개
};

struct FKAggregateGeom
{
	TArray<FKSphereElem>  SphereElems;
	TArray<FKBoxElem>     BoxElems;
	TArray<FKSphylElem>   SphylElems;
	TArray<FKConvexElem>  ConvexElems;
	TArray<FKTriMeshElem> TriMeshElems;

	bool IsEmpty() const
	{
		return SphereElems.IsEmpty() && BoxElems.IsEmpty() && SphylElems.IsEmpty()
			&& ConvexElems.IsEmpty() && TriMeshElems.IsEmpty();
	}

	// 에셋 단위(스케일 1) 도형에 컴포넌트 스케일을 곱한 사본. 위치·점은 축마다 곱한다.
	// 구·캡슐은 비균일 스케일을 표현할 수 없어 반지름에 큰 축을 쓴다 (UE와 같은 근사). 음수 스케일이면 삼각형 감기를 뒤집는다.
	FKAggregateGeom GetScaled(const FVector& Scale) const;
	// 스케일 적용 후 바디 로컬 공간에서 구·박스·캡슐 표면까지의 거리를 구한다.
	bool RaycastPrimitives(const FRay& LocalRay, float& OutDistance) const;

	// 바디 로컬 도형을 월드 와이어프레임으로 그린다. 스케일은 이미 곱해져 있어야 한다. (Collision 쇼 플래그)
	// 뷰 방향을 주면 캡슐은 그 방향에서 본 윤곽만 그린다.
	void DrawDebug(FLineBatcher& LineBatcher, const FVector& Location, const FQuat& Rotation, const FVector4& Color,
		const FVector* ViewDirection = nullptr) const;
};
