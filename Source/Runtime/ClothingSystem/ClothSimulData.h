#pragma once

#include "Math/Vector.h"

// 천 하나의 이번 프레임 시뮬레이션 결과 (컴포넌트 공간). 시뮬레이션이 만들고, 스켈레탈 메시 오브젝트가 렌더 정점에 덮어쓴다.
// 배열 순서는 시뮬레이션 정점 순서다. UE 4.27의 FClothSimulData 자리다.
struct FClothSimulData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
};
