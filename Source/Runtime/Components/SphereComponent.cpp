#include "EnginePCH.h"
#include "SphereComponent.h"
#include "Physics/Collision.h"
#include "Physics/AggregateGeom.h"
#include "Render/LineBatcher.h"

float USphereComponent::GetScaledSphereRadius() const
{
	const FVector S = GetWorldScale3D().GetAbs(); 
	return SphereRadius * std::max({S.X, S.Y, S.Z}); // 비균일 스케일이면 가장 큰 축
}

bool USphereComponent::GetBodyGeometry(FKAggregateGeom& OutGeom) const
{
	FKSphereElem Elem;
	Elem.Radius = GetScaledSphereRadius();
	OutGeom.SphereElems.Add(Elem);
	return true;
}

FBox USphereComponent::CalcLocalBounds() const
{
	const FVector R(SphereRadius, SphereRadius, SphereRadius);
	return FBox{ R * -1.0f, R };
}

void USphereComponent::DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const
{
	const FVector C = GetWorldLocation();
	const float R = GetScaledSphereRadius();
	constexpr float TwoPi = 2.0f * PI;
	AddWireArc(LineBatcher, C, FVector(1, 0, 0), FVector(0, 1, 0), R, 0, TwoPi, Color);
	AddWireArc(LineBatcher, C, FVector(1, 0, 0), FVector(0, 0, 1), R, 0, TwoPi, Color);
	AddWireArc(LineBatcher, C, FVector(0, 1, 0), FVector(0, 0, 1), R, 0, TwoPi, Color);
}
