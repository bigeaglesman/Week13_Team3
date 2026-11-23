#include "EnginePCH.h"
#include "CapsuleComponent.h"
#include "Render/LineBatcher.h"

float UCapsuleComponent::GetScaledCapsuleHalfHeight() const
{
	const float Half = CapsuleHalfHeight * std::abs(GetWorldScale3D().Z); 
	return std::max(Half, GetScaledCapsuleRadius()); // 반높이가 반지름보다 작으면 구가 된다
}

float UCapsuleComponent::GetScaledCapsuleRadius() const
{
	const FVector S = GetWorldScale3D().GetAbs(); 
	return CapsuleRadius * std::max(S.X, S.Y); // 반지름은 축에 수직인 X·Y 중 큰 쪽
}

FBox UCapsuleComponent::CalcLocalBounds() const
{
	const FVector E(CapsuleRadius, CapsuleRadius, CapsuleHalfHeight);
	return FBox{ E * -1.0f, E };
}

void UCapsuleComponent::DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const
{
	const FMatrix M = GetWorldMatrix();
	const FVector Center(M.M[3][0], M.M[3][1], M.M[3][2]);
	const FVector Up = FVector(M.M[2][0], M.M[2][1], M.M[2][2]).GetSafeNormal();
	const FVector X = FVector(M.M[0][0], M.M[0][1], M.M[0][2]).GetSafeNormal();
	const FVector Y = Up.Cross(X).GetSafeNormal();
	const float R = GetScaledCapsuleRadius();
	const float Segment = GetScaledCapsuleHalfHeight() - R;
	const FVector CapA = Center + Up * Segment;
	const FVector CapB = Center - Up * Segment;
	constexpr float Pi = PI;

	// 위·아래 고리
	AddWireArc(LineBatcher, CapA, X, Y, R, 0, 2 * Pi, Color);
	AddWireArc(LineBatcher, CapB, X, Y, R, 0, 2 * Pi, Color);
	// 위 반구 (0 → π: X → Up → -X), 아래 반구 (0 → -π: X → -Up → -X)
	AddWireArc(LineBatcher, CapA, X, Up, R, 0, Pi, Color);
	AddWireArc(LineBatcher, CapA, Y, Up, R, 0, Pi, Color);
	AddWireArc(LineBatcher, CapB, X, Up, R, 0, -Pi, Color);
	AddWireArc(LineBatcher, CapB, Y, Up, R, 0, -Pi, Color);
	// 옆선 4개
	for (const FVector& Side : { X, X * -1.0f, Y, Y * -1.0f })
		LineBatcher->AddLine(CapA + Side * R, CapB + Side * R, Color);
}

void UCapsuleComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "CapsuleHalfHeight" || Property.Name == "CapsuleRadius")
	{
		RecreatePhysicsState();
	}
}

bool UCapsuleComponent::BuildBodyShapeDesc(FBodyShapeDesc& OutDesc) const
{
	OutDesc.Type = EBodyShapeType::Capsule;
	OutDesc.Radius = GetScaledCapsuleRadius();
	OutDesc.HalfHeight = GetScaledCapsuleHalfHeight();
	return true;
}