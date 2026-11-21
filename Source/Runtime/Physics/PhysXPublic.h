#pragma once

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Transform.h"

#include <foundation/PxTransform.h>

// 주의: 이 헤더는 PhysX 헤더(<foundation/PxTransform.h>)를 직접 포함한다.
// 일반 엔진 헤더(.h)에서 include하지 말고, PhysX를 호출하는 .cpp에서만 include한다.
//
// 좌표계·단위 규칙:
// - 엔진과 PhysX 모두 월드 단위가 m(1 unit = 1m)이므로 스케일 배율은 1.0이다.
// - 엔진(Z-up 왼손)의 Cross/Quat 대수식이 PhysX와 동일하므로 언리얼처럼 성분을 1:1 매핑한다.
// - Scale은 PxTransform에 없으므로 Geometry 생성 시(4단계 FBodyInstance)에 반영한다.

inline physx::PxVec3 U2PVector(const FVector& UVec)
{
	return physx::PxVec3(UVec.X, UVec.Y, UVec.Z);
}

inline FVector P2UVector(const physx::PxVec3& PVec)
{
	return FVector(PVec.x, PVec.y, PVec.z);
}

inline physx::PxQuat U2PQuat(const FQuat& UQuat)
{
	physx::PxQuat PQuat(UQuat.X, UQuat.Y, UQuat.Z, UQuat.W);

	// PhysX는 비정규화·NaN 쿼터니언을 PxTransform에 넣으면 에러를 내고 동작을 거부한다.
	if (!PQuat.isFinite() || PQuat.magnitudeSquared() < 1e-8f)
	{
		return physx::PxQuat(physx::PxIdentity);
	}

	if (!PQuat.isUnit())
	{
		PQuat.normalize();
	}

	return PQuat;
}

inline FQuat P2UQuat(const physx::PxQuat& PQuat)
{
	return FQuat(PQuat.x, PQuat.y, PQuat.z, PQuat.w);
}

inline physx::PxTransform U2PTransform(const FTransform& UTransform)
{
	return physx::PxTransform(U2PVector(UTransform.Location), U2PQuat(UTransform.Rotation));
}

inline FTransform P2UTransform(const physx::PxTransform& PTransform)
{
	// PxTransform에는 Scale이 없으므로 FVector::OneVector로 복원한다.
	return FTransform(P2UQuat(PTransform.q), P2UVector(PTransform.p), FVector::OneVector);
}