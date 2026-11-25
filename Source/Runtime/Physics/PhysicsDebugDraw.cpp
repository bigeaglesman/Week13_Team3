#include "EnginePCH.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Physics/AggregateGeom.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PhysicsConstraintComponent.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Casts.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
	const FVector4 KinematicColor(0.0f, 1.0f, 1.0f, 1.0f);   // UE 충돌 와이어프레임의 Static 색
	const FVector4 SimulateColor(0.0f, 1.0f, 0.5f, 1.0f);    // UE의 Simulating 색

	// 삼각형 메시 와이어프레임은 이보다 많으면 너무 촘촘해서 그리지 않는다.
	constexpr int32 MaxDebugTriangles = 4096;

	float MaxAbs(float A, float B) { return std::max(std::fabs(A), std::fabs(B)); }
	FVector Mul(const FVector& A, const FVector& B) { return FVector(A.X * B.X, A.Y * B.Y, A.Z * B.Z); }

	void DrawCapsuleOutline(FLineBatcher& Lines, const FVector& Center, const FVector& Axis, float HalfLength,
		float Radius, const FVector& View, const FVector4& Color)
	{
		// 시선에 수직인 평면에서 두 끝을 반원으로 감싸고 접선 두 개로 잇는다.
		FVector Side = View.Cross(Axis);
		if (Side.SizeSquared() < 1e-8f)
		{
			const FVector Reference = std::fabs(View.Z) < 0.9f ? FVector(0, 0, 1) : FVector(0, 1, 0);
			Side = View.Cross(Reference);
		}
		Side = Side.Normalized();
		const FVector Along = Side.Cross(View);
		const FVector Offset = Axis * HalfLength;
		FVector Last = Side * Radius;
		constexpr int32 Steps = 16;
		for (int32 i = 1; i <= Steps; ++i)
		{
			const float Angle = PI * i / Steps;
			const FVector Next = (Side * std::cos(Angle) + Along * std::sin(Angle)) * Radius;
			Lines.AddLine(Center + Offset + Last, Center + Offset + Next, Color);
			Lines.AddLine(Center - Offset - Last, Center - Offset - Next, Color);
			Last = Next;
		}
		if (HalfLength > 0.0f)
		{
			for (const FVector& Edge : { Side * Radius, Side * -Radius })
				Lines.AddLine(Center + Offset + Edge, Center - Offset + Edge, Color);
		}
	}
}

FKAggregateGeom FKAggregateGeom::GetScaled(const FVector& Scale) const
{
	const FVector AbsScale(std::fabs(Scale.X), std::fabs(Scale.Y), std::fabs(Scale.Z));
	const float MaxScale = std::max({ AbsScale.X, AbsScale.Y, AbsScale.Z });

	FKAggregateGeom Result = *this;
	for (FKSphereElem& Elem : Result.SphereElems)
	{
		Elem.Center = Mul(Elem.Center, Scale);
		Elem.Radius *= MaxScale;
	}
	for (FKBoxElem& Elem : Result.BoxElems)
	{
		// 회전한 박스에 비균일 스케일을 주면 정확히는 박스가 아니게 된다. 박스 로컬 축에 그대로 곱하는 근사
		Elem.Center = Mul(Elem.Center, Scale);
		Elem.HalfExtent = Mul(Elem.HalfExtent, AbsScale);
	}
	for (FKSphylElem& Elem : Result.SphylElems)
	{
		Elem.Center = Mul(Elem.Center, Scale);
		Elem.Radius *= MaxAbs(AbsScale.X, AbsScale.Y);
		Elem.Length *= AbsScale.Z;
	}
	for (FKConvexElem& Elem : Result.ConvexElems)
	{
		for (FVector& P : Elem.VertexData)
		{
			P = Mul(P, Scale);
		}
	}

	const bool bFlipWinding = Scale.X * Scale.Y * Scale.Z < 0.0f;
	for (FKTriMeshElem& Elem : Result.TriMeshElems)
	{
		for (FVector& P : Elem.Vertices)
		{
			P = Mul(P, Scale);
		}
		if (bFlipWinding)
		{
			for (int32 i = 0; i + 2 < Elem.Indices.Num(); i += 3)
			{
				std::swap(Elem.Indices[i + 1], Elem.Indices[i + 2]);
			}
		}
	}
	return Result;
}

void FKAggregateGeom::DrawDebug(FLineBatcher& LineBatcher, const FVector& Location, const FQuat& Rotation, const FVector4& Color,
	const FVector* ViewDirection) const
{
	// 바디 로컬 → 월드. 쿼터니언 곱 순서 규칙에 기대지 않도록 벡터를 차례로 돌린다.
	auto ToWorld = [&](const FVector& Local) { return Location + Rotation.RotateVector(Local); };

	for (const FKSphereElem& Elem : SphereElems)
	{
		LineBatcher.AddSphere(ToWorld(Elem.Center), Elem.Radius, Color);
	}

	for (const FKBoxElem& Elem : BoxElems)
	{
		const FVector& H = Elem.HalfExtent;
		FVector Corners[8];
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector Local((i & 1) ? H.X : -H.X, (i & 2) ? H.Y : -H.Y, (i & 4) ? H.Z : -H.Z);
			Corners[i] = ToWorld(Elem.Center + Elem.Rotation.RotateVector(Local));
		}
		// 한 비트만 다른 꼭짓점끼리가 모서리다.
		for (int32 i = 0; i < 8; ++i)
		{
			for (int32 Bit = 1; Bit < 8; Bit <<= 1)
			{
				if (!(i & Bit))
				{
					LineBatcher.AddLine(Corners[i], Corners[i | Bit], Color);
				}
			}
		}
	}

	// 캡슐(로컬 Z축). 양 끝 구와 옆선 4개로 그린다.
	for (const FKSphylElem& Elem : SphylElems)
	{
		const FVector Center = ToWorld(Elem.Center);
		auto Dir = [&](const FVector& Local) { return Rotation.RotateVector(Elem.Rotation.RotateVector(Local)); };
		if (ViewDirection)
		{
			DrawCapsuleOutline(LineBatcher, Center, Dir(FVector(0, 0, 1)), 0.5f * Elem.Length, Elem.Radius, *ViewDirection, Color);
			continue;
		}
		const FVector Axis  = Dir(FVector(0.0f, 0.0f, 0.5f * Elem.Length));
		const FVector SideX = Dir(FVector(Elem.Radius, 0.0f, 0.0f));
		const FVector SideY = Dir(FVector(0.0f, Elem.Radius, 0.0f));

		LineBatcher.AddSphere(Center + Axis, Elem.Radius, Color);
		LineBatcher.AddSphere(Center - Axis, Elem.Radius, Color);
		for (const FVector& Side : { SideX, SideX * -1.0f, SideY, SideY * -1.0f })
		{
			LineBatcher.AddLine(Center + Axis + Side, Center - Axis + Side, Color);
		}
	}

	// 볼록 껍질: 쿠킹으로 구한 모서리가 있으면 그것을 그린다.
	for (const FKConvexElem& Elem : ConvexElems)
	{
		for (int32 i = 0; i + 1 < Elem.DebugEdges.Num(); i += 2)
		{
			const uint32 A = Elem.DebugEdges[i];
			const uint32 B = Elem.DebugEdges[i + 1];
			if (A < static_cast<uint32>(Elem.VertexData.Num()) && B < static_cast<uint32>(Elem.VertexData.Num()))
			{
				LineBatcher.AddLine(ToWorld(Elem.VertexData[A]), ToWorld(Elem.VertexData[B]), Color);
			}
		}
	}

	for (const FKTriMeshElem& Elem : TriMeshElems)
	{
		if (Elem.Indices.Num() / 3 > MaxDebugTriangles)
		{
			continue;
		}
		const uint32 NumVertices = static_cast<uint32>(Elem.Vertices.Num());
		for (int32 i = 0; i + 2 < Elem.Indices.Num(); i += 3)
		{
			const uint32 I0 = Elem.Indices[i], I1 = Elem.Indices[i + 1], I2 = Elem.Indices[i + 2];
			if (I0 >= NumVertices || I1 >= NumVertices || I2 >= NumVertices)
			{
				continue;
			}
			const FVector A = ToWorld(Elem.Vertices[I0]);
			const FVector B = ToWorld(Elem.Vertices[I1]);
			const FVector C = ToWorld(Elem.Vertices[I2]);
			LineBatcher.AddLine(A, B, Color);
			LineBatcher.AddLine(B, C, Color);
			LineBatcher.AddLine(C, A, Color);
		}
	}
}

void DrawPhysicsCollision(UWorld* World, FLineBatcher& LineBatcher)
{
	if (!World)
	{
		return;
	}

	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!Component || Component->GetWorld() != World || !Component->IsRegistered())
		{
			continue;
		}

		// UPrimitiveComponent::ShouldCreatePhysicsState와 같은 조건
		const FBodyInstance& Body = Component->BodyInstance;
		if (!Body.bSimulatePhysics && !Body.bEnablePhysicsCollision)
		{
			continue;
		}
		const FVector4& Color = Body.bSimulatePhysics ? SimulateColor : KinematicColor;

		// 스켈레탈 메시는 뼈마다 바디가 있다. 지금 애니메이션 포즈의 뼈 위치에 그린다.
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			UPhysicsAsset* PhysicsAsset = Skeletal->GetOrCreatePhysicsAsset();
			if (!PhysicsAsset)
			{
				continue;
			}

			const float Scale = Skeletal->GetPhysicsAssetScale();
			for (const UBodySetup* Setup : PhysicsAsset->BodySetups)
			{
				const int32 BoneIndex = Setup ? Skeletal->FindBoneIndex(Setup->BoneName) : -1;
				if (BoneIndex < 0)
				{
					continue;
				}
				const FTransform Bone = MatrixToBodyTransform(Skeletal->GetBoneWorldMatrix(BoneIndex));
				Setup->AggGeom.GetScaled(FVector(Scale, Scale, Scale)).DrawDebug(LineBatcher, Bone.Location, Bone.Rotation, Color);
			}
			continue;
		}

		// 스태틱 메시는 단순 충돌(볼록 껍질)을 그린다. 움직이지 않는 바디의 실제 도형은 삼각형 메시라
		// 매 프레임 메시 전체를 복사·그리기엔 무겁다. (UE의 Collision 쇼 플래그도 단순 충돌을 그린다)
		FKAggregateGeom Geom;
		UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Component);
		const bool bHasGeom = StaticMeshComp ? StaticMeshComp->GetSimpleBodyGeometry(Geom) : Component->GetBodyGeometry(Geom);
		if (bHasGeom)
		{
			// 바디 포즈에는 스케일이 없다 (도형에 이미 곱해져 있다).
			const FTransform Pose = Component->GetBodyWorldTransform();
			Geom.DrawDebug(LineBatcher, Pose.Location, Pose.Rotation, Color);
		}
	}

	// 관절: 피벗의 축(X 빨강 = Twist·이동 X, Y 초록 = Swing1, Z 파랑 = Swing2)과 이어진 바디까지의 선(주황).
	// 대상이 없는 쪽은 월드 고정점이라 선을 긋지 않는다.
	const FVector4 LinkColor(1.0f, 0.6f, 0.1f, 1.0f);
	for (TObjectIterator<UPhysicsConstraintComponent> It; It; ++It)
	{
		UPhysicsConstraintComponent* Constraint = *It;
		if (!Constraint || Constraint->GetWorld() != World || !Constraint->IsRegistered())
		{
			continue;
		}

		const FTransform Pivot = Constraint->GetPivotWorldTransform();
		const float AxisLength = 0.4f;
		LineBatcher.AddLine(Pivot.Location, Pivot.Location + Pivot.Rotation.RotateVector(FVector(AxisLength, 0.0f, 0.0f)), FVector4(1.0f, 0.2f, 0.2f, 1.0f));
		LineBatcher.AddLine(Pivot.Location, Pivot.Location + Pivot.Rotation.RotateVector(FVector(0.0f, AxisLength, 0.0f)), FVector4(0.2f, 1.0f, 0.2f, 1.0f));
		LineBatcher.AddLine(Pivot.Location, Pivot.Location + Pivot.Rotation.RotateVector(FVector(0.0f, 0.0f, AxisLength)), FVector4(0.2f, 0.4f, 1.0f, 1.0f));

		for (UPrimitiveComponent* Target : { Constraint->FindConstrainedComponent(Constraint->Actor1Name, false),
		                                     Constraint->FindConstrainedComponent(Constraint->Actor2Name, true) })
		{
			if (Target)
			{
				LineBatcher.AddLine(Pivot.Location, Target->GetWorldLocation(), LinkColor);
			}
		}
	}
}
