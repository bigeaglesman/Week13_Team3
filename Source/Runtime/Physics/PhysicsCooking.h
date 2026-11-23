#pragma once

#include "Containers/Array.h"
#include "Math/Vector.h"

namespace physx
{
	class PxConvexMesh;
	class PxTriangleMesh;
}

// 점·삼각형 → PhysX 충돌 메시 (PxCooking). UE 4.27의 FPhysXCookHelper 자리다.
// 돌려받은 메시는 참조 카운트가 1이다. 도형에 붙인 뒤 release하면 도형이 들고 있는 동안만 산다.
namespace FPhysicsCooking
{
	// 점들의 볼록 껍질. 점이 4개 미만이거나 한 평면 위에 있으면 nullptr
	physx::PxConvexMesh* CookConvexMesh(const TArray<FVector>& Points);

	// 점들의 볼록 껍질을 계산해 껍질 정점(최대 255개)과 디버그용 모서리(정점 인덱스 2개씩)를 돌려준다.
	// 에셋에 한 번 계산해 두면 런타임 바디는 적은 점으로 빠르게 다시 쿠킹한다. 실패하면 false
	bool ComputeConvexHull(const TArray<FVector>& Points, TArray<FVector>& OutHullVertices, TArray<uint32>& OutEdges);

	// 삼각형 메시. bFlipWinding이면 삼각형 감기 방향을 뒤집는다 (음수 스케일로 뒤집힌 메시).
	physx::PxTriangleMesh* CookTriangleMesh(const TArray<FVector>& Vertices, const TArray<uint32>& Indices, bool bFlipWinding = false);
}
