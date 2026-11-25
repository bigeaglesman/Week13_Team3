#pragma once

#include "Math/Vector.h"

// 천이 부딪히는 구 하나. 뼈를 따라 움직인다. UE 4.27의 FClothCollisionPrim_Sphere 자리다.
struct FClothCollisionPrim_Sphere
{
	int32   BoneIndex = -1;
	float   Radius = 0.0f;               // 메시 원본 단위 (스케일 전)
	FVector LocalPosition;               // 뼈 기준 위치 (메시 원본 단위)
};

// 구 두 개를 이은 캡슐. Physics Asset의 Sphyl은 양 끝 구 두 개 + 이 연결 하나가 된다.
// UE 4.27의 FClothCollisionPrim_SphereConnection 자리다.
struct FClothCollisionPrim_SphereConnection
{
	int32 SphereIndices[2] = { -1, -1 };
};

// 천 하나의 충돌체 목록. Physics Asset에서 뽑는다. UE 4.27의 FClothCollisionData 자리다.
struct FClothCollisionData
{
	TArray<FClothCollisionPrim_Sphere>           Spheres;
	TArray<FClothCollisionPrim_SphereConnection> SphereConnections;

	void Reset()
	{
		Spheres.Reset();
		SphereConnections.Reset();
	}
};
