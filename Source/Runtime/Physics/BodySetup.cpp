#include "EnginePCH.h"
#include "Physics/BodySetup.h"
#include "Components/PrimitiveComponent.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "Math/Transform.h"
#include <cmath>

namespace
{
	FVector AbsScale(const FVector& Scale)
	{
		return FVector(std::fabs(Scale.X), std::fabs(Scale.Y), std::fabs(Scale.Z));
	}
}

void FKAggregateGeom::DrawDebug(FLineBatcher& LineBatcher, const FVector& Location, const FQuat& Rotation, const FVector& Scale, const FVector4& Color) const
{
	const FVector Abs = AbsScale(Scale);

	// 바디 로컬 → 월드
	auto ToWorld = [&](const FVector& LocalCenter) { return Location + Rotation.RotateVector(LocalCenter * Scale); };

	for (const FKSphereElem& Elem : SphereElems)
		LineBatcher.AddSphere(ToWorld(Elem.Center), Elem.Radius * std::fmax(Abs.X, std::fmax(Abs.Y, Abs.Z)), Color);

	for (const FKBoxElem& Elem : BoxElems)
	{
		const FVector Half(0.5f * Elem.X * Abs.X, 0.5f * Elem.Y * Abs.Y, 0.5f * Elem.Z * Abs.Z);
		const FMatrix World = FTransform(Rotation * Elem.Rotation.Quaternion(), ToWorld(Elem.Center)).GetLocalMatrix();
		LineBatcher.AddOrientedBox(FBox{ Half * -1.0f, Half }, World, Color);
	}

	// 로컬 Z축 캡슐. 양 끝 구와 옆선 4개로 근사
	for (const FKSphylElem& Elem : SphylElems)
	{
		const FQuat ElemRotation = Rotation * Elem.Rotation.Quaternion();
		const FVector Center = ToWorld(Elem.Center);
		const float Radius = Elem.Radius * std::fmax(Abs.X, Abs.Y);
		const FVector Axis = ElemRotation.RotateVector(FVector(0.0f, 0.0f, 0.5f * Elem.Length * Abs.Z));
		const FVector SideX = ElemRotation.RotateVector(FVector(Radius, 0.0f, 0.0f));
		const FVector SideY = ElemRotation.RotateVector(FVector(0.0f, Radius, 0.0f));

		LineBatcher.AddSphere(Center + Axis, Radius, Color);
		LineBatcher.AddSphere(Center - Axis, Radius, Color);
		for (const FVector& Side : { SideX, SideX * -1.0f, SideY, SideY * -1.0f })
			LineBatcher.AddLine(Center + Axis + Side, Center - Axis + Side, Color);
	}
}

void DrawPhysicsCollision(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;

	// UE GetWireframeColor의 Static, 시뮬레이션 색
	const FVector4 StaticColor(0.0f, 1.0f, 1.0f, 1.0f);
	const FVector4 SimulateColor(0.0f, 1.0f, 0.5f, 1.0f);
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!Component || Component->GetWorld() != World || !Component->IsRegistered()) continue;
		// CreatePhysicsBody와 같은 조건
		if (!Component->bSimulatePhysics && !Component->bEnableStaticPhysicsCollision) continue;

		const UBodySetup* Setup = Component->GetBodySetup();
		if (!Setup) continue;

		const FVector4& Color = Component->bSimulatePhysics ? SimulateColor : StaticColor;
		Setup->AggGeom.DrawDebug(*LineBatcher, Component->GetWorldLocation(), Component->GetWorldRotation().Quaternion(), Component->GetWorldScale3D(), Color);
	}
}
