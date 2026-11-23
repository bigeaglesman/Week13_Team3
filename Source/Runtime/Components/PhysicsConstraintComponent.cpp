#include "EnginePCH.h"
#include "Components/PhysicsConstraintComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"

namespace
{
	UPrimitiveComponent* FindFirstValidPrimitive(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
		{
			if (RootPrim->GetBodyInstance()->IsValidBodyInstance())
			{
				return RootPrim;
			}
		}

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Comp))
			{
				if (Prim->GetBodyInstance()->IsValidBodyInstance())
				{
					return Prim;
				}
			}
		}

		return nullptr;
	}

	AActor* FindActorByName(UWorld* World, const FString& Name)
	{
		if (!World || Name.empty())
		{
			return nullptr;
		}

		ULevel* Level = World->GetPersistentLevel();
		if (!Level)
		{
			return nullptr;
		}

		for (AActor* Actor : Level->GetActors())
		{
			if (Actor && Actor->GetName() == Name)
			{
				return Actor;
			}
		}

		return nullptr;
	}

	// PhysX PxTransform은 스케일을 갖지 않으므로, 순수 강체 역변환(위치·회전만)으로
	// 조인트 월드 포즈를 바디 로컬 프레임으로 변환한다.
	FTransform ComputeRigidLocalFrame(const FTransform& ConstraintWorld, const FTransform& BodyWorld)
	{
		const FQuat InvBodyRot = BodyWorld.Rotation.Inverse();

		FTransform LocalFrame;
		LocalFrame.Location = InvBodyRot.RotateVector(ConstraintWorld.Location - BodyWorld.Location);
		LocalFrame.Rotation = (InvBodyRot * ConstraintWorld.Rotation).Normalized();
		LocalFrame.Scale = FVector::OneVector;
		return LocalFrame;
	}
}

void UPhysicsConstraintComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World || !World->GetPhysicsScene())
	{
		return;
	}

	AActor* Actor1 = FindActorByName(World, Actor1Name);
	AActor* Actor2 = Actor2Name.empty() ? GetOwner() : FindActorByName(World, Actor2Name);

	UPrimitiveComponent* Comp1 = FindFirstValidPrimitive(Actor1);
	UPrimitiveComponent* Comp2 = FindFirstValidPrimitive(Actor2);

	if (Comp1 || Comp2)
	{
		SetConstrainedComponents(Comp1, Comp2);
	}
}

void UPhysicsConstraintComponent::SetConstrainedComponents(UPrimitiveComponent* Comp1, UPrimitiveComponent* Comp2)
{
	FBodyInstance* Body1 = (Comp1 && Comp1->GetBodyInstance()->IsValidBodyInstance()) ? Comp1->GetBodyInstance() : nullptr;
	FBodyInstance* Body2 = (Comp2 && Comp2->GetBodyInstance()->IsValidBodyInstance()) ? Comp2->GetBodyInstance() : nullptr;

	if (!Body1 && !Body2)
	{
		return;
	}

	const FTransform ConstraintWorld = GetWorldTransform();

	// 상대 Body가 nullptr이면 RefFrame을 그대로 월드 공간 앵커 좌표로 사용한다.
	ConstraintInstance.RefFrame1 = Body1
		? ComputeRigidLocalFrame(ConstraintWorld, Body1->GetUnrealWorldTransform())
		: ConstraintWorld;
	ConstraintInstance.RefFrame2 = Body2
		? ComputeRigidLocalFrame(ConstraintWorld, Body2->GetUnrealWorldTransform())
		: ConstraintWorld;

	ConstraintInstance.bDisableCollision = bDisableCollision;
	ConstraintInstance.Swing1LimitDegrees = Swing1LimitDegrees;
	ConstraintInstance.Swing2LimitDegrees = Swing2LimitDegrees;
	ConstraintInstance.TwistLimitDegrees = TwistLimitDegrees;
	ConstraintInstance.AngularDamping = AngularDamping;
	ConstraintInstance.AngularStiffness = AngularStiffness;

	ConstraintInstance.InitConstraint(Body1, Body2);
}

void UPhysicsConstraintComponent::BreakConstraint()
{
	ConstraintInstance.TermConstraint();
}

void UPhysicsConstraintComponent::OnDestroyPhysicsState()
{
	ConstraintInstance.TermConstraint();
	Super::OnDestroyPhysicsState();
}