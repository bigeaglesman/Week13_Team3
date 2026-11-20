#include "EnginePCH.h"
#include "Physics/PhysXScene.h"
#include "Physics/BodyInstance.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Math/Transform.h"

#include <PxPhysicsAPI.h>
#include <extensions/PxDefaultCpuDispatcher.h>

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
    physx::PxScene* Scene = nullptr;
    physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
    physx::PxMaterial* DefaultMaterial = nullptr;

    std::vector<physx::PxRigidActor*> Actors;

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

bool FPhysXScene::Initialize(physx::PxPhysics* InPhysics)
{
    if (!InPhysics)
    {
        return false;
    }

    if (IsInitialized())
    {
        return Impl->Physics == InPhysics;
    }

    Impl->Physics = InPhysics;

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
                    Component->BodyInstance.reset();
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

    for (auto It = Impl->Actors.begin(); It != Impl->Actors.end();)
    {
        physx::PxRigidActor* Actor = *It;
        FBodyInstance* BodyInstance = Actor ? static_cast<FBodyInstance*>(Actor->userData) : nullptr;

        if (Actor && BodyInstance && (BodyInstance == TargetInstance || BodyInstance->GetOwnerComponent() == Component))
        {
            BodyInstance->Terminate();
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

    if (!ActiveActors)
    {
        return;
    }

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

        if (!Component || !Component->IsRegistered())
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

physx::PxScene* FPhysXScene::GetNativeScene() const
{
    return Impl ? Impl->Scene : nullptr;
}

bool FPhysXScene::IsInitialized() const
{
    return Impl && Impl->Physics && Impl->Scene && Impl->Dispatcher && Impl->DefaultMaterial;
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

