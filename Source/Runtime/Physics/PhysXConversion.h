#pragma once

// 엔진 수학 타입 <-> PhysX 타입.
// 행렬을 거치지 않고 위치(p)와 회전(q)을 성분 그대로 옮겨 row/column-major 차이를 피한다.
// 월드 단위가 m라 단위 변환은 없다. Runtime/Physics 안의 .cpp에서만 include한다.
#include <foundation/PxVec3.h>
#include <foundation/PxQuat.h>
#include <foundation/PxTransform.h>
#include <foundation/PxMat33.h>

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Matrix.h"

inline physx::PxVec3 ToPx(const FVector& V)            { return physx::PxVec3(V.X, V.Y, V.Z); }
inline FVector       ToFVector(const physx::PxVec3& V) { return FVector(V.x, V.y, V.z); }

inline physx::PxQuat ToPx(const FQuat& Q)              { return physx::PxQuat(Q.X, Q.Y, Q.Z, Q.W); }
inline FQuat         ToFQuat(const physx::PxQuat& Q)   { return FQuat(Q.x, Q.y, Q.z, Q.w); }

// PhysX 트랜스폼에는 스케일이 없다. 스케일은 도형 크기에 미리 곱해 둔다.
inline physx::PxTransform ToPx(const FTransform& T)
{
	return physx::PxTransform(ToPx(T.Location), ToPx(T.Rotation).getNormalized());
}

inline FTransform ToFTransform(const physx::PxTransform& T, const FVector& Scale = FVector(1.0f, 1.0f, 1.0f))
{
	return FTransform(ToFQuat(T.q), ToFVector(T.p), Scale);
}

// 행렬(뼈 월드 행렬 등) → 위치 + 회전. 축 길이(스케일)는 버린다.
// 엔진 FMatrix는 row-vector라 M[0..2]행이 X·Y·Z축, M[3]행이 위치다. PxMat33은 열이 축이다.
inline physx::PxTransform ToPxNoScale(const FMatrix& M)
{
	physx::PxVec3 X(M.M[0][0], M.M[0][1], M.M[0][2]);
	physx::PxVec3 Y(M.M[1][0], M.M[1][1], M.M[1][2]);
	physx::PxVec3 Z(M.M[2][0], M.M[2][1], M.M[2][2]);
	X.normalize();
	Y.normalize();
	Z.normalize();
	return physx::PxTransform(physx::PxVec3(M.M[3][0], M.M[3][1], M.M[3][2]), physx::PxQuat(physx::PxMat33(X, Y, Z)).getNormalized());
}
