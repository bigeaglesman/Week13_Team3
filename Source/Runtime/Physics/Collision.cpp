#include "EnginePCH.h"
#include "Collision.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"

namespace
{
	FVector Row(const FMatrix& M, int32 R) { return FVector(M.M[R][0], M.M[R][1], M.M[R][2]); }
	
	// 정렬 순서가 곧 분기 규칙: 항상 작은 쪽이 앞에 오게 바꾸면 조합이 9개가 아니라 6개가 된다
	enum class EShapeKind : int32 { Sphere = 0, Capsule = 1, Box = 2, Unknown = 3 };

	EShapeKind KindOf(const UShapeComponent* S)
	{
		UClass* C = S->GetClass();
		if (C->IsChildOf(USphereComponent::StaticClass()))  return EShapeKind::Sphere;
		if (C->IsChildOf(UCapsuleComponent::StaticClass())) return EShapeKind::Capsule;
		if (C->IsChildOf(UBoxComponent::StaticClass()))     return EShapeKind::Box;
		return EShapeKind::Unknown;
	}

	FVector ClosestPointOnBox(const FVector& P, const FWorldBox& B)
	{
		const FVector D = P - B.Center;
		FVector Q = B.Center;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Dist = std::clamp(D.Dot(B.Axis[i]), -B.Extent[i], B.Extent[i]);
			Q += B.Axis[i] * Dist;
		}
		return Q;
	}

	FVector ClosestPointOnSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float LenSq = AB.SizeSquared();
		if (LenSq < 1e-8f) return A;
		const float T = std::clamp((P - A).Dot(AB) / LenSq, 0.0f, 1.0f);
		return A + AB * T;
	}

	constexpr float Eps = 1e-6f;
	float Clamp01(float V) { return std::clamp(V, 0.0f, 1.0f); }

	// 선분 P1-Q1과 P2-Q2 사이 최단 거리의 제곱
	float SegmentSegmentDistSq(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2)
	{
		const FVector D1 = Q1 - P1;
		const FVector D2 = Q2 - P2;
		const FVector R = P1 - P2;
		const float A = D1.Dot(D1);
		const float E = D2.Dot(D2);
		const float F = D2.Dot(R);
		float S = 0.0f, T = 0.0f;

		if (A <= Eps && E <= Eps)
			return R.Dot(R);
		if (A <= Eps)
		{
			T = Clamp01(F / E);
		}
		else
		{
			const float C = D1.Dot(R);
			if (E <= Eps)
			{
				S = Clamp01(-C / A);
			}
			else
			{
				const float B = D1.Dot(D2);
				const float Denom = A * E - B * B;
				S = Denom > Eps ? Clamp01((B * F - C * E) / Denom) : 0.0f;
				T = (B * S + F) / E;
				if (T < 0.0f) { T = 0.0f; S = Clamp01(-C / A); }
				else if (T > 1.0f) { T = 1.0f; S = Clamp01((B - C) / A); }
			}
		}

		const FVector C1 = P1 + D1 * S;
		const FVector C2 = P2 + D2 * T;
		return (C1 - C2).SizeSquared();
	}
}

namespace FCollision
{
	bool SphereSphere(const FWorldSphere& A, const FWorldSphere& B)
	{
		const float R = A.Radius + B.Radius;
		return (A.Center - B.Center).SizeSquared() <= R * R;   // 제곱끼리 비교해서 sqrt를 피한다
	}

	bool SphereBox(const FWorldSphere& S, const FWorldBox& B)
	{
		const FVector Q = ClosestPointOnBox(S.Center, B);
		return (S.Center - Q).SizeSquared() <= S.Radius * S.Radius;
	}

	bool CapsuleSphere(const FWorldCapsule& C, const FWorldSphere& S)
	{
		const FVector Q = ClosestPointOnSegment(S.Center, C.A, C.B);
		return SphereSphere({ Q, C.Radius }, S);
	}

	bool CapsuleCapsule(const FWorldCapsule& A, const FWorldCapsule& B)
	{
		const float R = A.Radius + B.Radius;
		return SegmentSegmentDistSq(A.A, A.B, B.A, B.B) <= R * R;
	}

	bool BoxBox(const FWorldBox& A, const FWorldBox& B)
	{
		float R[3][3], AbsR[3][3];
		for (int32 i = 0; i < 3; ++i)
			for (int32 j = 0; j < 3; ++j)
			{
				R[i][j] = A.Axis[i].Dot(B.Axis[j]);
				AbsR[i][j] = std::abs(R[i][j]) + 1e-6f;
			}

		const FVector Tw = B.Center - A.Center;
		const float T[3] = { Tw.Dot(A.Axis[0]), Tw.Dot(A.Axis[1]), Tw.Dot(A.Axis[2]) };
		const float Ea[3] = { A.Extent[0], A.Extent[1], A.Extent[2] };
		const float Eb[3] = { B.Extent[0], B.Extent[1], B.Extent[2] };

		// A의 면 법선 3개
		for (int32 i = 0; i < 3; ++i)
		{
			const float Ra = Ea[i];
			const float Rb = Eb[0] * AbsR[i][0] + Eb[1] * AbsR[i][1] + Eb[2] * AbsR[i][2];
			if (std::abs(T[i]) > Ra + Rb) return false;
		}

		// B의 면 법선 3개
		for (int32 j = 0; j < 3; ++j)
		{
			const float Ra = Ea[0] * AbsR[0][j] + Ea[1] * AbsR[1][j] + Ea[2] * AbsR[2][j];
			const float Rb = Eb[j];
			if (std::abs(T[0] * R[0][j] + T[1] * R[1][j] + T[2] * R[2][j]) > Ra + Rb) return false;
		}

		// 모서리 외적 9개
		for (int32 i = 0; i < 3; ++i)
		{
			const int32 I1 = (i + 1) % 3, I2 = (i + 2) % 3;
			for (int32 j = 0; j < 3; ++j)
			{
				const int32 J1 = (j + 1) % 3, J2 = (j + 2) % 3;
				const float Ra = Ea[I1] * AbsR[I2][j] + Ea[I2] * AbsR[I1][j];
				const float Rb = Eb[J1] * AbsR[i][J2] + Eb[J2] * AbsR[i][J1];
				if (std::abs(T[I2] * R[I1][j] - T[I1] * R[I2][j]) > Ra + Rb) return false;
			}
		}

		return true;
	}

	bool CapsuleBox(const FWorldCapsule& C, const FWorldBox& B)
	{
		const FVector AB = C.B - C.A;
		auto DistSq = [&](float T)
		{
			const FVector P = C.A + AB * T;
			return (P - ClosestPointOnBox(P, B)).SizeSquared();
		};

		float Lo = 0.0f, Hi = 1.0f;
		for (int32 It = 0; It < 24; ++It)
		{
			const float M1 = Lo + (Hi - Lo) / 3.0f;
			const float M2 = Hi - (Hi - Lo) / 3.0f;
			if (DistSq(M1) < DistSq(M2)) Hi = M2;
			else Lo = M1;
		}
		return DistSq((Lo + Hi) * 0.5f) <= C.Radius * C.Radius;
	}

	bool Overlap(const UShapeComponent* A, const UShapeComponent* B)
	{
		if (!A || !B || A == B)
			return false;

		EShapeKind KA = KindOf(A), KB = KindOf(B);
		if (KA == EShapeKind::Unknown || KB == EShapeKind::Unknown)
			return false;
		if (KA > KB) { std::swap(A, B); std::swap(KA, KB); }

		auto AsSphere = [](const UShapeComponent* S) { return ToWorld(static_cast<const USphereComponent*>(S)); };
		auto AsCapsule = [](const UShapeComponent* S) { return ToWorld(static_cast<const UCapsuleComponent*>(S)); };
		auto AsBox = [](const UShapeComponent* S) { return ToWorld(static_cast<const UBoxComponent*>(S)); };

		switch (KA)
		{
		case EShapeKind::Sphere:
			if (KB == EShapeKind::Sphere)  return SphereSphere(AsSphere(A), AsSphere(B));
			if (KB == EShapeKind::Capsule) return CapsuleSphere(AsCapsule(B), AsSphere(A));
			return SphereBox(AsSphere(A), AsBox(B));
		case EShapeKind::Capsule:
			if (KB == EShapeKind::Capsule) return CapsuleCapsule(AsCapsule(A), AsCapsule(B));
			return CapsuleBox(AsCapsule(A), AsBox(B));
		default:
			return BoxBox(AsBox(A), AsBox(B));
		}
	}

	FWorldSphere ToWorld(const USphereComponent* S)
	{
		return { S->GetWorldLocation(), S->GetScaledSphereRadius() };
	}

	FWorldCapsule ToWorld(const UCapsuleComponent* C)
	{
		const FMatrix M = C->GetWorldMatrix();
		const FVector Center = Row(M, 3);
		const FVector Up = Row(M, 2).GetSafeNormal();
		const float Radius = C->GetScaledCapsuleRadius();
		const float Segment = C->GetScaledCapsuleHalfHeight() - Radius;   // 반구 중심까지 거리 (0이면 구)
		return { Center + Up * Segment, Center - Up * Segment, Radius };
	}

	FWorldBox ToWorld(const UBoxComponent* B)
	{
		const FMatrix M = B->GetWorldMatrix();
		FWorldBox Box;
		Box.Center = Row(M, 3);
		for (int32 i = 0; i < 3; ++i)
			Box.Axis[i] = Row(M, i).GetSafeNormal();   // 스케일은 Extent 쪽에 들어간다
		Box.Extent = B->GetScaledBoxExtent();
		return Box;
	}
}
