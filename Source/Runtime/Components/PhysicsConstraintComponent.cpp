#include "EnginePCH.h"
#include "Components/PhysicsConstraintComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/BodyInstance.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "UObject/Casts.h"

namespace
{
	AActor* FindActorByName(UWorld* World, const FString& Name)
	{
		ULevel* Level = (World && !Name.empty()) ? World->GetPersistentLevel() : nullptr;
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

	// 물리 바디를 만드는(또는 만든) 컴포넌트인지
	bool HasPhysicsBody(const UPrimitiveComponent* Component)
	{
		const FBodyInstance& Body = Component->BodyInstance;
		return Body.IsValidBodyInstance() || Body.bSimulatePhysics || Body.bEnablePhysicsCollision;
	}
}

UPhysicsConstraintComponent::UPhysicsConstraintComponent()
{
	// 첫 Tick에 연결한다. 그 뒤로는 Tick을 끈다.
	PrimaryComponentTick.bCanEverTick = true;
}

UPrimitiveComponent* UPhysicsConstraintComponent::FindConstrainedComponent(const FString& ActorName, bool bUseOwnerIfEmpty) const
{
	AActor* Actor = ActorName.empty()
		? (bUseOwnerIfEmpty ? GetOwner() : nullptr)
		: FindActorByName(GetWorld(), ActorName);
	if (!Actor)
	{
		return nullptr;
	}

	// 루트가 바디를 가지면 루트를, 아니면 바디를 가진 첫 컴포넌트를 쓴다 (UE도 루트 프리미티브를 먼저 본다).
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		if (HasPhysicsBody(Root))
		{
			return Root;
		}
	}
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			if (HasPhysicsBody(Primitive))
			{
				return Primitive;
			}
		}
	}
	return nullptr;
}

FTransform UPhysicsConstraintComponent::GetPivotWorldTransform() const
{
	// 부모가 없으면 쿼터니언을 그대로 쓴다. FRotator를 거치면 Pitch ±90도 근처에서 값이 튄다.
	const FQuat Rotation = GetAttachParent() ? GetWorldRotation().Quaternion() : GetRelativeRotationQuat();
	return FTransform(Rotation, GetWorldLocation());
}

FConstraintSetup UPhysicsConstraintComponent::MakeConstraintSetup() const
{
	FConstraintSetup Setup;
	Setup.JointName = GetName();
	Setup.LinearXMotion = LinearXMotion;
	Setup.LinearYMotion = LinearYMotion;
	Setup.LinearZMotion = LinearZMotion;
	Setup.LinearLimit = LinearLimit;
	Setup.TwistMotion = TwistMotion;
	Setup.Swing1Motion = Swing1Motion;
	Setup.Swing2Motion = Swing2Motion;
	Setup.TwistLimitDeg = TwistLimitDegrees;
	Setup.Swing1LimitDeg = Swing1LimitDegrees;
	Setup.Swing2LimitDeg = Swing2LimitDegrees;
	Setup.bDisableCollision = bDisableCollision;
	Setup.AngularDriveStiffness = AngularDriveStiffness;
	Setup.AngularDriveDamping = AngularDriveDamping;
	return Setup;
}

bool UPhysicsConstraintComponent::SetConstrainedComponents(UPrimitiveComponent* Component1, UPrimitiveComponent* Component2)
{
	BreakConstraint();

	FBodyInstance* Body1 = (Component1 && Component1->BodyInstance.IsValidBodyInstance()) ? &Component1->BodyInstance : nullptr;
	FBodyInstance* Body2 = (Component2 && Component2->BodyInstance.IsValidBodyInstance()) ? &Component2->BodyInstance : nullptr;
	if (!Body1 && !Body2)
	{
		return false;
	}

	// UE와 같이 1 = 부모(actor0), 2 = 자식(actor1)
	ConstraintInstance.InitConstraintAtPivot(MakeConstraintSetup(), Body2, Body1, GetPivotWorldTransform());
	return ConstraintInstance.IsValidConstraintInstance();
}

void UPhysicsConstraintComponent::BreakConstraint()
{
	ConstraintInstance.TermConstraint();
}

void UPhysicsConstraintComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	// 모든 액터의 BeginPlay(바디 생성)가 끝난 뒤인 첫 Tick에 한 번만 잇는다.
	if (!bConnectAttempted)
	{
		bConnectAttempted = true;
		CreatePhysicsState();   // 물리 씬이 있는 월드에서만 OnCreatePhysicsState가 불린다
		SetComponentTickEnabled(false);
	}
}

void UPhysicsConstraintComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	UPrimitiveComponent* Component1 = FindConstrainedComponent(Actor1Name, false);
	UPrimitiveComponent* Component2 = FindConstrainedComponent(Actor2Name, true);

	if (!SetConstrainedComponents(Component1, Component2))
	{
		HTR_LOG(Warning, "[PhysX] Constraint '{}' could not connect. Actor1='{}', Actor2='{}' need physics bodies (Simulate Physics or Physics Collision).",
			GetName(), Actor1Name, Actor2Name.empty() ? (GetOwner() ? GetOwner()->GetName() : FString()) : Actor2Name);
	}
}

void UPhysicsConstraintComponent::OnDestroyPhysicsState()
{
	BreakConstraint();
	Super::OnDestroyPhysicsState();
}
