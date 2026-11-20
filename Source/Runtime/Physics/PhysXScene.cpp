#include "EnginePCH.h"
#include "Physics/PhysXScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsAsset.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Math/Transform.h"
#include "Render/StaticMeshData.h"

#include <cooking/PxCooking.h>
#include <PxPhysicsAPI.h>
#include <extensions/PxDefaultCpuDispatcher.h>
#include <extensions/PxD6Joint.h>

#include <algorithm>
#include <vector>
#include <mutex>

namespace
{
    physx::PxFilterFlags PhysXSimulationFilterShader(physx::PxFilterObjectAttributes Attributes0,
        physx::PxFilterData, physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData, physx::PxPairFlags& PairFlags, const void*, physx::PxU32)
    {
        if (physx::PxFilterObjectIsTrigger(Attributes0) || physx::PxFilterObjectIsTrigger(Attributes1))
        {
            PairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND | physx::PxPairFlag::eNOTIFY_TOUCH_LOST;
        }
        else
        {
            PairFlags = physx::PxPairFlag::eCONTACT_DEFAULT | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND | physx::PxPairFlag::eNOTIFY_TOUCH_LOST;
        }

        return physx::PxFilterFlag::eDEFAULT;
    }

    physx::PxD6Motion::Enum ToPxD6Motion(EPhysicsConstraintMotion Motion)
    {
        switch (Motion)
        {
        case EPhysicsConstraintMotion::Locked: return physx::PxD6Motion::eLOCKED;
        case EPhysicsConstraintMotion::Limited: return physx::PxD6Motion::eLIMITED;
        case EPhysicsConstraintMotion::Free: return physx::PxD6Motion::eFREE;
        default: return physx::PxD6Motion::eLOCKED;
        }
    }
}

struct FPhysXScene::FImpl : physx::PxSimulationEventCallback
{
    enum class EQueuedEventType : uint8
    {
        ContactBegin,
        ContactEnd,
        TriggerBegin,
        TriggerEnd
    };

    struct FQueuedEvent
    {
        UPrimitiveComponent* ComponentA = nullptr;
        UPrimitiveComponent* ComponentB = nullptr;
        EQueuedEventType Type = EQueuedEventType::ContactBegin;
    };

    physx::PxPhysics* Physics = nullptr;
    physx::PxCooking* Cooking = nullptr;
    physx::PxScene* Scene = nullptr;
    physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
    physx::PxMaterial* DefaultMaterial = nullptr;

    struct FJointEntry
    {
        physx::PxJoint* Joint = nullptr;
        USkeletalMeshComponent* Owner = nullptr;
    };

    std::vector<physx::PxRigidActor*> Actors;
    std::vector<FJointEntry> Joints;

    std::mutex EventMutex;
    std::vector<FQueuedEvent> PendingEvents;

    float Accumulator = 0.0f;

    void QueueEvent(UPrimitiveComponent* A, UPrimitiveComponent* B, EQueuedEventType Type)
    {
        if (!A || !B || A == B)
            return;

        std::lock_guard<std::mutex> Lock(EventMutex);
        PendingEvents.push_back({ A, B, Type });
    }

    void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}

    void onWake(physx::PxActor**, physx::PxU32) override {}

    void onSleep(physx::PxActor**, physx::PxU32) override {}

    void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override {}

    void onContact(const physx::PxContactPairHeader& PairHeader, const physx::PxContactPair* Pairs, physx::PxU32 PairCount) override
    {
        if (!PairHeader.actors[0] || !PairHeader.actors[1])
            return;

        if ((PairHeader.flags & physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0) || (PairHeader.flags & physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
            return;

        FBodyInstance* BodyA = static_cast<FBodyInstance*>(PairHeader.actors[0]->userData);
        FBodyInstance* BodyB = static_cast<FBodyInstance*>(PairHeader.actors[1]->userData);

        UPrimitiveComponent* A = BodyA ? BodyA->GetOwnerComponent() : nullptr;
        UPrimitiveComponent* B = BodyB ? BodyB->GetOwnerComponent() : nullptr;

        if (!A || !B)
            return;

        for (physx::PxU32 Index = 0; Index < PairCount; ++Index)
        {
            if ((Pairs[Index].flags & physx::PxContactPairFlag::eREMOVED_SHAPE_0) || (Pairs[Index].flags & physx::PxContactPairFlag::eREMOVED_SHAPE_1))
                continue;

            if (Pairs[Index].flags & physx::PxContactPairFlag::eACTOR_PAIR_HAS_FIRST_TOUCH)
                QueueEvent(A, B, EQueuedEventType::ContactBegin);

            if (Pairs[Index].flags & physx::PxContactPairFlag::eACTOR_PAIR_LOST_TOUCH)
                QueueEvent(A, B, EQueuedEventType::ContactEnd);
        }
    }

    void onTrigger(physx::PxTriggerPair* Pairs, physx::PxU32 Count) override
    {
        for (physx::PxU32 Index = 0; Index < Count; ++Index)
        {
            const physx::PxTriggerPair& Pair = Pairs[Index];

            if (!Pair.triggerActor || !Pair.otherActor)
                continue;

            if ((Pair.flags & physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER) || (Pair.flags & physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
                continue;

            FBodyInstance* BodyA = static_cast<FBodyInstance*>(Pair.triggerActor->userData);
            FBodyInstance* BodyB = static_cast<FBodyInstance*>(Pair.otherActor->userData);

            UPrimitiveComponent* A = BodyA ? BodyA->GetOwnerComponent() : nullptr;
            UPrimitiveComponent* B = BodyB ? BodyB->GetOwnerComponent() : nullptr;

            if (Pair.status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
                QueueEvent(A, B, EQueuedEventType::TriggerBegin);
            else if (Pair.status == physx::PxPairFlag::eNOTIFY_TOUCH_LOST)
                QueueEvent(A, B, EQueuedEventType::TriggerEnd);
        }
    }
};

FPhysXScene::FPhysXScene()
    : Impl(MakeUnique<FImpl>())
{
}

FPhysXScene::~FPhysXScene()
{
    Shutdown();
}

bool FPhysXScene::Initialize(physx::PxPhysics* InPhysics, physx::PxCooking* InCooking)
{
    if (!InPhysics || !InCooking)
    {
        return false;
    }

    if (IsInitialized())
    {
        return Impl->Physics == InPhysics && Impl->Cooking == InCooking;
    }

    Impl->Physics = InPhysics;
    Impl->Cooking = InCooking;
    Impl->DefaultMaterial = InPhysics->createMaterial(0.5f, 0.5f, 0.5f);

    if (!Impl->DefaultMaterial)
    {
        Shutdown();
        return false;
    }

    Impl->Dispatcher = physx::PxDefaultCpuDispatcherCreate(2);

    if (!Impl->Dispatcher)
    {
        Shutdown();
        return false;
    }

    physx::PxSceneDesc SceneDesc(InPhysics->getTolerancesScale());

    SceneDesc.gravity = physx::PxVec3(0.0f, 0.0f, -9.81f);
    SceneDesc.cpuDispatcher = Impl->Dispatcher;
    SceneDesc.filterShader = PhysXSimulationFilterShader;
    SceneDesc.simulationEventCallback = Impl.get();

    SceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;

    Impl->Scene = InPhysics->createScene(SceneDesc);

    if (!Impl->Scene)
    {
        Shutdown();
        return false;
    }

    HTR_LOG(Info, "[PhysX] Physics Scene initialized.");

    return true;
}

void FPhysXScene::Shutdown()
{
    if (!Impl)
    {
        return;
    }

    ClearActors();

    if (Impl->Scene)
    {
        Impl->Scene->release();
        Impl->Scene = nullptr;
    }

    if (Impl->DefaultMaterial)
    {
        Impl->DefaultMaterial->release();
        Impl->DefaultMaterial = nullptr;
    }

    if (Impl->Dispatcher)
    {
        Impl->Dispatcher->release();
        Impl->Dispatcher = nullptr;
    }

    Impl->Physics = nullptr;
    Impl->Cooking = nullptr;
    Impl->Accumulator = 0.0f;
}

void FPhysXScene::ClearActors()
{
    if (!Impl)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> Lock(Impl->EventMutex);
        Impl->PendingEvents.clear();
    }

    for (FImpl::FJointEntry& Entry : Impl->Joints)
    {
        if (Entry.Joint)
        {
            Entry.Joint->release();
        }
    }

    Impl->Joints.clear();

    for (physx::PxRigidActor* Actor : Impl->Actors)
    {
        if (Actor)
        {
            FBodyInstance* BodyInstance = static_cast<FBodyInstance*>(Actor->userData);

            if (BodyInstance)
            {
                UPrimitiveComponent* Component = BodyInstance->GetOwnerComponent();
                BodyInstance->Terminate();

                if (Component && Component->BodyInstance.get() == BodyInstance)
                {
                    Component->BodyInstance.reset();
                }
                else if (USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(Component))
                {
                    TArray<TUniquePtr<FBodyInstance>>& Bodies = SkeletalComponent->PhysicsBodies;

                    for (int32 Index = 0; Index < Bodies.Num(); ++Index)
                    {
                        if(Bodies[Index].get() == BodyInstance)
                        {
                            Bodies.RemoveAt(Index);
                            break;
                        }
                    }
                }
            }

            Actor->release();
        }
    }

    Impl->Actors.clear();
}

void FPhysXScene::Simulate(float DeltaTime)
{
    if (!IsInitialized() || DeltaTime <= 0.0f)
    {
        return;
    }

    constexpr float FixedDeltaTime = 1.0f / 60.0f;
    constexpr uint32 MaxSubSteps = 4;

    Impl->Accumulator += std::min(DeltaTime, FixedDeltaTime * static_cast<float>(MaxSubSteps));

    uint32 Steps = 0;

    while (Impl->Accumulator >= FixedDeltaTime && Steps < MaxSubSteps)
    {
        Impl->Scene->simulate(FixedDeltaTime);

        if (!Impl->Scene->fetchResults(true))
        {
            HTR_LOG(Error, "[PhysX] Failed to fetch simulation results.");
            break;
        }
        Impl->Accumulator -= FixedDeltaTime;
        ++Steps;
    }
}

void FPhysXScene::BindBody(physx::PxRigidStatic* Body, UPrimitiveComponent* Component)
{
    BindBodyInternal(Body, Component);
}

void FPhysXScene::BindBody(physx::PxRigidDynamic* Body, UPrimitiveComponent* Component)
{
    BindBodyInternal(Body, Component);
}

bool FPhysXScene::BindBody(physx::PxRigidDynamic* Body, USkeletalMeshComponent* Component, int32 BoneIndex)
{
    if (!IsInitialized() || !Body || !Component || Body->getScene() != Impl->Scene || Body->userData)
    {
        return false;
    }

    if (BoneIndex < 0 || BoneIndex >= Component->GetBones().Num())
    {
        return false;
    }

    auto BodyInstance = MakeUnique<FBodyInstance>();
    BodyInstance->Initialize(Body, Component, BoneIndex);

    if (!BodyInstance->IsValid())
    {
        return false;
    }

    Component->PhysicsBodies.Add(std::move(BodyInstance));
    return true;
}

void FPhysXScene::RemoveBodyForComponent(UPrimitiveComponent* Component)
{
    if (!Component || !Impl)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> Lock(Impl->EventMutex);
        auto& Events = Impl->PendingEvents;
        Events.erase(std::remove_if(Events.begin(), Events.end(), [Component](const FImpl::FQueuedEvent& Event) {return Event.ComponentA == Component || Event.ComponentB == Component;}), Events.end());
    }

    FBodyInstance* TargetInstance = Component->BodyInstance.get();

    if (USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(Component))
    {
        SkeletalComponent->bRagdollActive = false;

        for (auto It = Impl->Joints.begin(); It != Impl->Joints.end();)
        {
            if (It->Owner == SkeletalComponent)
            {
                if (It->Joint)
                {
                    It->Joint->release();
                }

                It = Impl->Joints.erase(It);
            }
            else
            {
                ++It;
            }
        }
    }
   

    for (auto It = Impl->Actors.begin(); It != Impl->Actors.end();)
    {
        physx::PxRigidActor* Actor = *It;
        FBodyInstance* BodyInstance = Actor ? static_cast<FBodyInstance*>(Actor->userData) : nullptr;

        if (Actor && BodyInstance && (BodyInstance == TargetInstance || BodyInstance->GetOwnerComponent() == Component))
        {
            BodyInstance->Terminate();

            if (USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(Component))
            {
                TArray<TUniquePtr<FBodyInstance>>& Bodies = SkeletalComponent->PhysicsBodies;

                for (int32 Index = 0; Index < Bodies.Num(); ++Index)
                {
                    if (Bodies[Index].get() == BodyInstance)
                    {
                        Bodies.RemoveAt(Index);
                        break;
                    }
                }
            }

            Actor->release();
            It = Impl->Actors.erase(It);
        }
        else
        {
            ++It;
        }
    }
    if (Component->BodyInstance)
    {
        Component->BodyInstance->Terminate();
        Component->BodyInstance.reset();
    }
}

void FPhysXScene::SyncTransforms()
{
    if (!IsInitialized())
    {
        return;
    }

    physx::PxU32 ActiveActorCount = 0;
    physx::PxActor** ActiveActors = Impl->Scene->getActiveActors(ActiveActorCount);

    if(ActiveActors)
    {
        for (physx::PxU32 Index = 0; Index < ActiveActorCount; ++Index)
        {
            physx::PxActor* Actor = ActiveActors[Index];

            if (!Actor || Actor->getType() != physx::PxActorType::eRIGID_DYNAMIC)
            {
                continue;
            }

            physx::PxRigidDynamic* Body = static_cast<physx::PxRigidDynamic*>(Actor);
            FBodyInstance* BodyInstance = static_cast<FBodyInstance*>(Body->userData);
            UPrimitiveComponent* Component = BodyInstance ? BodyInstance->GetOwnerComponent() : nullptr;
            const int32 BoneIndex = BodyInstance ? BodyInstance->GetBoneIndex() : -1;

            if (!Component || !Component->IsRegistered() || BoneIndex >= 0)
            {
                continue;
            }

            const physx::PxTransform Pose = Body->getGlobalPose();
            const FTransform WorldTransform(
                FQuat(Pose.q.x, Pose.q.y, Pose.q.z, Pose.q.w),
                FVector(Pose.p.x, Pose.p.y, Pose.p.z),
                Component->GetWorldScale3D()
            );

            Component->SetWorldTransform(WorldTransform.GetLocalMatrix(), false);
        }
    }

    std::vector<USkeletalMeshComponent*> RagdollComponents;

    for (physx::PxRigidActor* Actor : Impl->Actors)
    {
        if (!Actor || Actor->getType() != physx::PxActorType::eRIGID_DYNAMIC)
        {
            continue;
        }

        FBodyInstance* BodyInstance = static_cast<FBodyInstance*>(Actor->userData);

        if (!BodyInstance || BodyInstance->GetBoneIndex() < 0)
        {
            continue;
        }

        USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(BodyInstance->GetOwnerComponent());

        if (!SkeletalComponent || std::find(RagdollComponents.begin(), RagdollComponents.end(), SkeletalComponent) != RagdollComponents.end())
        {
            continue;
        }

        RagdollComponents.push_back(SkeletalComponent);
    }

    for (USkeletalMeshComponent* SkeletalComponent : RagdollComponents)
    {
        if (!SkeletalComponent->IsRegistered() || !SkeletalComponent->bRagdollActive)
        {
            continue;
        }

        const TArray<FBoneInfo>& Bones = SkeletalComponent->GetBones();

        if (Bones.IsEmpty())
        {
            continue;
        }

        TArray<FTransform> BoneWorldTransforms;
        BoneWorldTransforms.SetNum(Bones.Num());

        const FTransform ComponentWorld(
            FQuat(SkeletalComponent->GetWorldRotation()),
            SkeletalComponent->GetWorldLocation(),
            SkeletalComponent->GetWorldScale3D()
        );

        for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
        {
            const int32 ParentIndex = Bones[BoneIndex].ParentIndex;
            const FTransform ParentWorld = ParentIndex >= 0 && ParentIndex < BoneIndex ? BoneWorldTransforms[ParentIndex] : ComponentWorld;

            FTransform LocalTransform = SkeletalComponent->GetBoneLocalTransform(BoneIndex);
            FBodyInstance* BoneBody = nullptr;

            for (const TUniquePtr<FBodyInstance>& Candidate : SkeletalComponent->PhysicsBodies)
            {
                if (Candidate && Candidate->IsValid() && Candidate->GetBoneIndex() == BoneIndex)
                {
                    BoneBody = Candidate.get();
                    break;
                }
            }

            if (BoneBody && BoneBody->Actor->getType() == physx::PxActorType::eRIGID_DYNAMIC)
            {
                const physx::PxTransform Pose = static_cast<physx::PxRigidDynamic*>(BoneBody->Actor)->getGlobalPose();

                const FTransform PhysicsWorld(
                    FQuat(Pose.q.x, Pose.q.y, Pose.q.z, Pose.q.w),
                    FVector(Pose.p.x, Pose.p.y, Pose.p.z),
                    ComponentWorld.Scale
                );

                LocalTransform = PhysicsWorld.GetRelativeTransform(ParentWorld);

                SkeletalComponent->SetBoneLocalTransform(BoneIndex, LocalTransform);
                BoneWorldTransforms[BoneIndex] = PhysicsWorld;
            }
            else
            {
                const FVector ScaledLocalLocation = LocalTransform.Location * ParentWorld.Scale;
                const FVector WorldLocation = ParentWorld.Location + ParentWorld.Rotation.RotateVector(ScaledLocalLocation);
                const FQuat WorldRotation = (ParentWorld.Rotation * LocalTransform.Rotation).Normalized();
                const FVector WorldScale = LocalTransform.Scale * ParentWorld.Scale;

                BoneWorldTransforms[BoneIndex] = FTransform(WorldRotation, WorldLocation, WorldScale);
            }
        }
        SkeletalComponent->RefreshBoneTransformsNow();
    }
}

void FPhysXScene::DispatchQueuedEvents()
{
    if (!Impl)
    {
        return;
    }

    std::vector<FImpl::FQueuedEvent> Events;

    {
        std::lock_guard<std::mutex> Lock(Impl->EventMutex);
        Events.swap(Impl->PendingEvents);
    }

    for (const FImpl::FQueuedEvent& Event : Events)
    {
        UPrimitiveComponent* A = Event.ComponentA;
        UPrimitiveComponent* B = Event.ComponentB;

        if (!A || !B || !A->IsRegistered() || !B->IsRegistered())
            continue;

        switch (Event.Type)
        {
        case FImpl::EQueuedEventType::ContactBegin:
            if (A->bGenerateOverlapEvents && B->bGenerateOverlapEvents)
            {
                A->BeginComponentOverlap(B);
                B->BeginComponentOverlap(A);
            }

            if (A->bBlockComponent && B->bBlockComponent)
            {
                A->DispatchHit(B);
                B->DispatchHit(A);
            }
            break;

        case FImpl::EQueuedEventType::ContactEnd:
        case FImpl::EQueuedEventType::TriggerEnd:
            A->EndComponentOverlap(B);
            B->EndComponentOverlap(A);
            break;

        case FImpl::EQueuedEventType::TriggerBegin:
            if (A->bGenerateOverlapEvents && B->bGenerateOverlapEvents)
            {
                A->BeginComponentOverlap(B);
                B->BeginComponentOverlap(A);
            }
            break;
        }
    }
}

physx::PxRigidStatic* FPhysXScene::CreateStaticBox(const FVector& Position, const FVector& HalfExtents)
{
    if (!IsInitialized() || HalfExtents.X <= 0.0f || HalfExtents.Y <= 0.0f || HalfExtents.Z <= 0.0f)
        return nullptr;

    const physx::PxTransform Pose(physx::PxVec3(Position.X, Position.Y, Position.Z));

    physx::PxRigidStatic* Actor = Impl->Physics->createRigidStatic(Pose);

    if (!Actor)
        return nullptr;

    physx::PxShape* Shape = Impl->Physics->createShape(physx::PxBoxGeometry(HalfExtents.X, HalfExtents.Y, HalfExtents.Z), *Impl->DefaultMaterial, true);

    if (!Shape)
    {
        Actor->release();
        return nullptr;
    }

    if (!Actor->attachShape(*Shape))
    {
        Shape->release();
        Actor->release();
        return nullptr;
    }

    Shape->release();

    Impl->Scene->addActor(*Actor);
    Impl->Actors.push_back(Actor);

    return Actor;
}

physx::PxRigidDynamic* FPhysXScene::CreateDynamicBox(const FVector& Position, const FVector& HalfExtents, float Density)
{
    const FTransform Transform(Position, FVector(1.0f, 1.0f, 1.0f), FRotator(0.0f, 0.0f, 0.0f));
    return CreateDynamicBox(Transform, HalfExtents, Density);
}

physx::PxRigidDynamic* FPhysXScene::CreateDynamicBox(const FTransform& WorldTransform, const FVector& HalfExtents, float Density)
{
    const FVector ScaledHalfExtents(
        std::abs(HalfExtents.X * WorldTransform.Scale.X),
        std::abs(HalfExtents.Y * WorldTransform.Scale.Y),
        std::abs(HalfExtents.Z * WorldTransform.Scale.Z)
    );

    if (!IsInitialized() || Density <= 0.0f || ScaledHalfExtents.X <= 0.0f || ScaledHalfExtents.Y <= 0.0f || ScaledHalfExtents.Z <= 0.0f)
        return nullptr;

    const FQuat Rotation = WorldTransform.Rotation.Normalized();
    const physx::PxQuat PxRotation(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
    const physx::PxTransform Pose(physx::PxVec3(WorldTransform.Location.X, WorldTransform.Location.Y, WorldTransform.Location.Z), PxRotation);

    physx::PxRigidDynamic* Actor = Impl->Physics->createRigidDynamic(Pose);

    if (!Actor)
        return nullptr;

    physx::PxShape* Shape = Impl->Physics->createShape(physx::PxBoxGeometry(ScaledHalfExtents.X, ScaledHalfExtents.Y, ScaledHalfExtents.Z), *Impl->DefaultMaterial, true);

    if (!Shape)
    {
        Actor->release();
        return nullptr;
    }

    if (!Actor->attachShape(*Shape))
    {
        Shape->release();
        Actor->release();
        return nullptr;
    }

    if (!physx::PxRigidBodyExt::updateMassAndInertia(*Actor, Density))
    {
        Shape->release();
        Actor->release();
        return nullptr;
    }

    Shape->release();

    Impl->Scene->addActor(*Actor);
    Impl->Actors.push_back(Actor);

    return Actor;
}

bool FPhysXScene::CreateRagdollBody(const FPhysicsBodySetup& BodySetup, const FTransform& WorldTransform, USkeletalMeshComponent* Component, int32 BoneIndex)
{
    const FVector& WorldScale = WorldTransform.Scale;

    if (!IsInitialized() || !Component || BoneIndex < 0 || BoneIndex >= Component->GetBones().Num() || BodySetup.Shapes.IsEmpty() ||
        !std::isfinite(BodySetup.Mass) || BodySetup.Mass <= 0.0f ||
        !std::isfinite(WorldScale.X) || !std::isfinite(WorldScale.Y) || !std::isfinite(WorldScale.Z) ||
        std::abs(WorldScale.X) <= 1e-6f || std::abs(WorldScale.Y) <= 1e-6f || std::abs(WorldScale.Z) <= 1e-6f)
    {
        return false;
    }

    const FQuat Rotation = WorldTransform.Rotation.Normalized();
    const physx::PxQuat PxRotation(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
    const physx::PxTransform Pose(physx::PxVec3(WorldTransform.Location.X, WorldTransform.Location.Y, WorldTransform.Location.Z), PxRotation);

    physx::PxRigidDynamic* Actor = Impl->Physics->createRigidDynamic(Pose);

    if (!Actor)
    {
        return false;
    }

    for (const FPhysicsShapeSetup& ShapeSetup : BodySetup.Shapes)
    {
        const FVector& Dimensions = ShapeSetup.Dimensions;

        const FVector ShapeScale(
            WorldScale.X * ShapeSetup.LocalPose.Scale.X,
            WorldScale.Y * ShapeSetup.LocalPose.Scale.Y,
            WorldScale.Z * ShapeSetup.LocalPose.Scale.Z
        );

        const FVector AbsScale(std::abs(ShapeScale.X), std::abs(ShapeScale.Y), std::abs(ShapeScale.Z));

        if (!std::isfinite(Dimensions.X) || !std::isfinite(Dimensions.Y) || !std::isfinite(Dimensions.Z) ||
            !std::isfinite(ShapeScale.X) || !std::isfinite(ShapeScale.Y) || !std::isfinite(ShapeScale.Z) ||
            AbsScale.X <= 1e-6f || AbsScale.Y <= 1e-6f || AbsScale.Z <= 1e-6f)
        {
            Actor->release();
            return false;
        }

        physx::PxMaterial* Material = Impl->Physics->createMaterial(ShapeSetup.StaticFriction, ShapeSetup.DynamicFriction, ShapeSetup.Restitution);

        if (!Material)
        {
            Actor->release();
            return false;
        }

        physx::PxShape* Shape = nullptr;

        switch (ShapeSetup.ShapeType)
        {
        case EPhysicsShapeType::Box:
            if (Dimensions.X > 0.0f && Dimensions.Y > 0.0f && Dimensions.Z > 0.0f)
            {
                Shape = Impl->Physics->createShape(physx::PxBoxGeometry(Dimensions.X * AbsScale.X, Dimensions.Y * AbsScale.Y, Dimensions.Z * AbsScale.Z), *Material, true);
            }
            break;

        case EPhysicsShapeType::Sphere:
            if (Dimensions.X > 0.0f)
            {
                const float RadiusScale = std::max(AbsScale.X, std::max(AbsScale.Y, AbsScale.Z));
                Shape = Impl->Physics->createShape(physx::PxSphereGeometry(Dimensions.X * RadiusScale), *Material, true);
            }
            break;

        case EPhysicsShapeType::Capsule:
            if (Dimensions.X > 0.0f && Dimensions.Z >= 0.0f)
            {
                const float RadiusScale = std::max(AbsScale.X, AbsScale.Y);
                Shape = Impl->Physics->createShape(physx::PxCapsuleGeometry(Dimensions.X * RadiusScale, Dimensions.Z * AbsScale.Z), *Material, true);
            }
            break;
        }

        Material->release();

        if (!Shape)
        {
            Actor->release();
            return false;
        }

        const FVector& LocalLocation = ShapeSetup.LocalPose.Location;
        const FQuat LocalRotation = ShapeSetup.LocalPose.Rotation.Normalized();
        const physx::PxVec3 PxLocalPosition(
            LocalLocation.X * WorldScale.X,
            LocalLocation.Y * WorldScale.Y,
            LocalLocation.Z * WorldScale.Z
        );

        physx::PxQuat PxLocalRotation(LocalRotation.X, LocalRotation.Y, LocalRotation.Z, LocalRotation.W);

        if (ShapeSetup.ShapeType == EPhysicsShapeType::Capsule)
        {
            PxLocalRotation = PxLocalRotation * physx::PxQuat(1.57079632679f, physx::PxVec3(0.0f, 1.0f, 0.0f));
        }

        Shape->setLocalPose(physx::PxTransform(PxLocalPosition, PxLocalRotation));

        if (!Actor->attachShape(*Shape))
        {
            Shape->release();
            Actor->release();
            return false;
        }
        Shape->release();
    }

    if (!physx::PxRigidBodyExt::setMassAndUpdateInertia(*Actor, BodySetup.Mass))
    {
        Actor->release();
        return false;
    }

    Actor->setLinearDamping(BodySetup.LinearDamping);
    Actor->setAngularDamping(BodySetup.AngularDamping);
    Actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, BodySetup.bKinematic);
    Actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !BodySetup.bEnableGravity);

    Impl->Scene->addActor(*Actor);

    if (!BindBody(Actor, Component, BoneIndex))
    {
        Impl->Scene->removeActor(*Actor);
        Actor->release();
        return false;
    }

    Impl->Actors.push_back(Actor);

    if (!BodySetup.bStartAwake)
    {
        Actor->putToSleep();
    }

    return true;
}

bool FPhysXScene::CreateRagdollConstraint(const FPhysicsConstraintSetup& ConstraintSetup, USkeletalMeshComponent* Component)
{
    if (!IsInitialized() || !Component)
    {
        return false;
    }

    const int32 BoneIndexA = Component->FindBoneIndex(ConstraintSetup.BoneNameA);
    const int32 BoneIndexB = Component->FindBoneIndex(ConstraintSetup.BoneNameB);

    if (BoneIndexA < 0 || BoneIndexB < 0 || BoneIndexA == BoneIndexB)
    {
        return false;
    }

    FBodyInstance* BodyInstanceA = nullptr;
    FBodyInstance* BodyInstanceB = nullptr;

    for (const TUniquePtr<FBodyInstance>& BodyInstance : Component->PhysicsBodies)
    {
        if (!BodyInstance || !BodyInstance->IsValid())
        {
            continue;
        }

        if (BodyInstance->GetBoneIndex() == BoneIndexA)
        {
            BodyInstanceA = BodyInstance.get();
        }
        else if (BodyInstance->GetBoneIndex() == BoneIndexB)
        {
            BodyInstanceB = BodyInstance.get();
        }
    }

    if (!BodyInstanceA || !BodyInstanceB)
    {
        return false;
    }

    if (BodyInstanceA->Actor->getType() != physx::PxActorType::eRIGID_DYNAMIC ||
        BodyInstanceB->Actor->getType() != physx::PxActorType::eRIGID_DYNAMIC)
    {
        return false;
    }

    physx::PxRigidDynamic* ActorA = static_cast<physx::PxRigidDynamic*>(BodyInstanceA->Actor);
    physx::PxRigidDynamic* ActorB = static_cast<physx::PxRigidDynamic*>(BodyInstanceB->Actor);
    
    if (ActorA == ActorB || ActorA->getScene() != Impl->Scene || ActorB->getScene() != Impl->Scene)
    {
        return false;
    }

    const FVector& WorldScale = Component->GetWorldScale3D();
    const FQuat RotationA = ConstraintSetup.FrameA.Rotation.Normalized();
    const FQuat RotationB = ConstraintSetup.FrameB.Rotation.Normalized();

    const physx::PxTransform LocalFrameA(
        physx::PxVec3(ConstraintSetup.FrameA.Location.X * WorldScale.X, ConstraintSetup.FrameA.Location.Y * WorldScale.Y, ConstraintSetup.FrameA.Location.Z * WorldScale.Z),
        physx::PxQuat(RotationA.X, RotationA.Y, RotationA.Z, RotationA.W)
    );

    const physx::PxTransform LocalFrameB(
        physx::PxVec3(ConstraintSetup.FrameB.Location.X * WorldScale.X, ConstraintSetup.FrameB.Location.Y * WorldScale.Y, ConstraintSetup.FrameB.Location.Z * WorldScale.Z),
        physx::PxQuat(RotationB.X, RotationB.Y, RotationB.Z, RotationB.W)
    );

    physx::PxD6Joint* Joint = physx::PxD6JointCreate(*Impl->Physics, ActorA, LocalFrameA, ActorB, LocalFrameB);

    if (!Joint)
    {
        return false;
    }

    Joint->setMotion(physx::PxD6Axis::eX, ToPxD6Motion(ConstraintSetup.LinearX));
    Joint->setMotion(physx::PxD6Axis::eY, ToPxD6Motion(ConstraintSetup.LinearY));
    Joint->setMotion(physx::PxD6Axis::eZ, ToPxD6Motion(ConstraintSetup.LinearZ));

    Joint->setMotion(physx::PxD6Axis::eSWING1, ToPxD6Motion(ConstraintSetup.Swing1));
    Joint->setMotion(physx::PxD6Axis::eSWING2, ToPxD6Motion(ConstraintSetup.Swing2));
    Joint->setMotion(physx::PxD6Axis::eTWIST, ToPxD6Motion(ConstraintSetup.Twist));

    const physx::PxJointLinearLimitPair LinearLimit(Impl->Physics->getTolerancesScale(), -ConstraintSetup.LinearLimit, ConstraintSetup.LinearLimit);

    if (ConstraintSetup.LinearX == EPhysicsConstraintMotion::Limited)
    {
        Joint->setLinearLimit(physx::PxD6Axis::eX, LinearLimit);
    }

    if (ConstraintSetup.LinearY == EPhysicsConstraintMotion::Limited)
    {
        Joint->setLinearLimit(physx::PxD6Axis::eY, LinearLimit);
    }

    if (ConstraintSetup.LinearZ == EPhysicsConstraintMotion::Limited)
    {
        Joint->setLinearLimit(physx::PxD6Axis::eZ, LinearLimit);
    }

    constexpr float DegreesToRadians = 0.017453292519943295f;

    if (ConstraintSetup.Swing1 == EPhysicsConstraintMotion::Limited || ConstraintSetup.Swing2 == EPhysicsConstraintMotion::Limited)
    {
        const physx::PxJointLimitCone SwingLimit(ConstraintSetup.Swing1Limit * DegreesToRadians, ConstraintSetup.Swing2Limit * DegreesToRadians);
        Joint->setSwingLimit(SwingLimit);
    }

    if (ConstraintSetup.Twist == EPhysicsConstraintMotion::Limited)
    {
        const float TwistLimit = ConstraintSetup.TwistLimit * DegreesToRadians;
        Joint->setTwistLimit(physx::PxJointAngularLimitPair(-TwistLimit, TwistLimit));
    }

    Joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, false);

    Impl->Joints.push_back({ Joint, Component });

    return true;
}

physx::PxRigidStatic* FPhysXScene::CreateStaticTriangleMesh(const FStaticMeshData& MeshData, const FTransform& WorldTransform)
{
    if (!IsInitialized() || MeshData.Vertices.Num() < 3 || MeshData.Indices.Num() < 3 || MeshData.Indices.Num() % 3 != 0)
        return nullptr;

    const FVector& Scale = WorldTransform.Scale;

    if (!std::isfinite(Scale.X) || !std::isfinite(Scale.Y) || !std::isfinite(Scale.Z) ||
        std::abs(Scale.X) <= 1e-6f || std::abs(Scale.Y) <= 1e-6f || std::abs(Scale.Z) <= 1e-6f)
        return nullptr;

    std::vector<physx::PxVec3> Vertices;
    Vertices.reserve(MeshData.Vertices.Num());

    for (const FVertex& Vertex : MeshData.Vertices)
    {
        const FVector& Position = Vertex.Position;
        const physx::PxVec3 ScaledPosition(Position.X * Scale.X, Position.Y * Scale.Y, Position.Z * Scale.Z);

        if (!ScaledPosition.isFinite())
            return nullptr;

        Vertices.push_back(ScaledPosition);
    }

    std::vector<physx::PxU32> Triangles;
    Triangles.reserve(MeshData.Indices.Num());

    const bool bReverseWinding = Scale.X * Scale.Y * Scale.Z < 0.0f;

    for (int32 Index = 0; Index < MeshData.Indices.Num(); Index += 3)
    {
        const uint32 I0 = MeshData.Indices[Index];
        const uint32 I1 = MeshData.Indices[Index + 1];
        const uint32 I2 = MeshData.Indices[Index + 2];

        if (I0 >= static_cast<uint32>(Vertices.size()) ||
            I1 >= static_cast<uint32>(Vertices.size()) ||
            I2 >= static_cast<uint32>(Vertices.size()))
            return nullptr;

        Triangles.push_back(I0);
        Triangles.push_back(bReverseWinding ? I2 : I1);
        Triangles.push_back(bReverseWinding ? I1 : I2);
    }

    physx::PxTriangleMeshDesc MeshDesc;
    MeshDesc.points.count = static_cast<physx::PxU32>(Vertices.size());
    MeshDesc.points.stride = sizeof(physx::PxVec3);
    MeshDesc.points.data = Vertices.data();

    MeshDesc.triangles.count = static_cast<physx::PxU32>(Triangles.size() / 3);
    MeshDesc.triangles.stride = sizeof(physx::PxU32) * 3;
    MeshDesc.triangles.data = Triangles.data();

    if (!MeshDesc.isValid())
    {
        HTR_LOG(Error, "[PhysX] Static triangle mesh descriptor is invalid. Vertices={}, Triangles={}", MeshDesc.points.count, MeshDesc.triangles.count);
        return nullptr;
    }

    physx::PxTriangleMeshCookingResult::Enum CookingResult = physx::PxTriangleMeshCookingResult::eSUCCESS;
    physx::PxTriangleMesh* CookedMesh = Impl->Cooking->createTriangleMesh(MeshDesc, Impl->Physics->getPhysicsInsertionCallback(), &CookingResult);

    if (!CookedMesh)
    {
        HTR_LOG(Error, "[PhysX] Static triangle mesh cooking failed. Result={}", static_cast<int32>(CookingResult));
        return nullptr;
    }

    if (CookingResult == physx::PxTriangleMeshCookingResult::eLARGE_TRIANGLE)
    {
        HTR_LOG(Warning, "[PhysX] Static triangle mesh contains large triangles.");
    }

    if (!CookedMesh)
    {
        HTR_LOG(Error, "[PhysX] Static triangle mesh cooking failed.");
        return nullptr;
    }

    const FQuat Rotation = WorldTransform.Rotation.Normalized();
    const physx::PxQuat PxRotation(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
    const physx::PxTransform Pose(physx::PxVec3(WorldTransform.Location.X, WorldTransform.Location.Y, WorldTransform.Location.Z), PxRotation);

    physx::PxRigidStatic* Actor = Impl->Physics->createRigidStatic(Pose);

    if (!Actor)
    {
        CookedMesh->release();
        return nullptr;
    }

    const physx::PxTriangleMeshGeometry Geometry(CookedMesh);
    physx::PxShape* Shape = Impl->Physics->createShape(Geometry, *Impl->DefaultMaterial, true);

    if (!Shape)
    {
        CookedMesh->release();
        Actor->release();
        return nullptr;
    }

    if (!Actor->attachShape(*Shape))
    {
        Shape->release();
        CookedMesh->release();
        Actor->release();
        return nullptr;
    }

    Shape->release();
    CookedMesh->release();

    Impl->Scene->addActor(*Actor);
    Impl->Actors.push_back(Actor);

    return Actor;
}


physx::PxRigidDynamic* FPhysXScene::CreateDynamicConvexMesh(const FStaticMeshData& MeshData, const FTransform& WorldTransform, float Density)
{
    if (!IsInitialized() || Density <= 0.0f || MeshData.Vertices.Num() < 4)
    {
        return nullptr;
    }

    const FVector& Scale = WorldTransform.Scale;

    if (!std::isfinite(Scale.X) || !std::isfinite(Scale.Y) || !std::isfinite(Scale.Z) ||
        std::abs(Scale.X) <= 1e-6f || std::abs(Scale.Y) <= 1e-6f || std::abs(Scale.Z) <= 1e-6f)
    {
        return nullptr;
    }

    std::vector<physx::PxVec3> Vertices;
    Vertices.reserve(MeshData.Vertices.Num());

    for (const FVertex& Vertex : MeshData.Vertices)
    {
        const FVector& Position = Vertex.Position;
        const physx::PxVec3 ScaledPosition(Position.X * Scale.X, Position.Y * Scale.Y, Position.Z * Scale.Z);

        if (!ScaledPosition.isFinite())
        {
            return nullptr;
        }

        Vertices.push_back(ScaledPosition);
    }

    physx::PxConvexMeshDesc MeshDesc;
    MeshDesc.points.count = static_cast<physx::PxU32>(Vertices.size());
    MeshDesc.points.stride = sizeof(physx::PxVec3);
    MeshDesc.points.data = Vertices.data();
    MeshDesc.flags |= physx::PxConvexFlag::eCOMPUTE_CONVEX;
    MeshDesc.flags |= physx::PxConvexFlag::eSHIFT_VERTICES;
    MeshDesc.vertexLimit = 255;

    if (!MeshDesc.isValid())
    {
        return nullptr;
    }

    physx::PxConvexMesh* CookedMesh = Impl->Cooking->createConvexMesh(MeshDesc, Impl->Physics->getPhysicsInsertionCallback());

    if (!CookedMesh)
    {
        HTR_LOG(Error, "[PhysX] Dynamic convex mesh cooking failed.");
        return nullptr;
    }

    const FQuat Rotation = WorldTransform.Rotation.Normalized();
    const physx::PxQuat PxRotation(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
    const physx::PxTransform Pose(physx::PxVec3(WorldTransform.Location.X, WorldTransform.Location.Y, WorldTransform.Location.Z), PxRotation);

    physx::PxRigidDynamic* Actor = Impl->Physics->createRigidDynamic(Pose);

    if (!Actor)
    {
        CookedMesh->release();
        return nullptr;
    }

    const physx::PxConvexMeshGeometry Geometry(CookedMesh);
    physx::PxShape* Shape = Impl->Physics->createShape(Geometry, *Impl->DefaultMaterial, true);

    if (!Shape)
    {
        CookedMesh->release();
        Actor->release();
        return nullptr;
    }

    if (!Actor->attachShape(*Shape))
    {
        Shape->release();
        CookedMesh->release();
        Actor->release();
        return nullptr;
    }

    if (!physx::PxRigidBodyExt::updateMassAndInertia(*Actor, Density))
    {
        Shape->release();
        CookedMesh->release();
        Actor->release();
        return nullptr;
    }

    Shape->release();
    CookedMesh->release();

    Impl->Scene->addActor(*Actor);
    Impl->Actors.push_back(Actor);

    return Actor;
}

physx::PxScene* FPhysXScene::GetNativeScene() const
{
    return Impl ? Impl->Scene : nullptr;
}

bool FPhysXScene::IsInitialized() const
{
    return Impl && Impl->Physics && Impl->Cooking && Impl->Scene && Impl->Dispatcher && Impl->DefaultMaterial;
}

bool FPhysXScene::BindBodyInternal(physx::PxRigidActor* Body, UPrimitiveComponent* Component)
{
    if (!IsInitialized() || !Body || !Component || Body->getScene() != Impl->Scene || Body->userData)
    {
        return false;
    }

    if (Component->BodyInstance && Component->BodyInstance->IsValid())
    {
        return false;
    }

    Component->BodyInstance.reset();
    Component->BodyInstance = MakeUnique<FBodyInstance>();
    Component->BodyInstance->Initialize(Body, Component);

    return Component->BodyInstance->IsValid();
}

