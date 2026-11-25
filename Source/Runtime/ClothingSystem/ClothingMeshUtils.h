#pragma once

#include "Math/Vector.h"
#include <cmath>
#include <tuple>

// 천 메시를 만들 때 쓰는 작은 도구들. UE 4.27의 ClothingMeshUtils 자리다.
namespace ClothingMeshUtils
{
	// 위치를 아주 작은 격자로 반올림한 키. 키가 같으면 같은 자리의 정점으로 보고 하나로 합친다(용접).
	// 렌더 메시는 UV·노멀 경계에서 같은 자리에 정점이 여러 개 있는데, 천은 그 자리가 찢어지면 안 된다.
	using FWeldKey = std::tuple<int64, int64, int64>;

	inline constexpr float WeldTolerance = 1.0e-4f;

	inline FWeldKey MakeWeldKey(const FVector& Position)
	{
		return FWeldKey(
			static_cast<int64>(std::llround(Position.X / WeldTolerance)),
			static_cast<int64>(std::llround(Position.Y / WeldTolerance)),
			static_cast<int64>(std::llround(Position.Z / WeldTolerance)));
	}
}
