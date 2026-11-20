#include "EnginePCH.h"
#include "Physics/PhysXScene.h"
#include "Components/SceneComponent.h"
#include "Math/Transform.h"

#include <PxPhysicsAPI.h>
#include <extensions/PxDefaultCpuDispatcher.h>

#include <algorithm>
#include <vector>

struct FPhysXScene::FImpl
{
    physx::PxPhysics* Physics = nullptr;
    physx::PxScene* Scene = nullptr;
    physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
    physx::PxMaterial* DefaultMaterial = nullptr;

    std::vector<physx::PxRigidActor*> Actors;

    physx::PxRigidDynamic* LastDynamicBody = nullptr;

    float Accumulator = 0.0f;
    uint32 DebugFrameCounter = 0;
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
    SceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

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
    Impl->DebugFrameCounter = 0;
}

void FPhysXScene::ClearActors()
{
    if (!Impl)
    {
        return;
    }

    for (physx::PxRigidActor* Actor : Impl->Actors)
    {
        if (Actor)
        {
            Actor->release();
        }
    }

    Impl->Actors.clear();
    Impl->LastDynamicBody = nullptr;
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

#if defined(ENGINE_DEBUG)
    if (Impl->LastDynamicBody && ++Impl->DebugFrameCounter >= 60)
    {
        const physx::PxVec3 Position = Impl->LastDynamicBody->getGlobalPose().p;

        HTR_LOG(Info, "[PhysX] Dynamic Body Position: X={}, Y={}, Z={}", Position.x, Position.y, Position.z);

        Impl->DebugFrameCounter = 0;
    }
#endif
}

void FPhysXScene::BindDynamicBody(physx::PxRigidDynamic* Body, USceneComponent* Component)
{
    if (!IsInitialized() || !Body || !Component || Body->getScene() != Impl->Scene)
    {
        return;
    }

    Body->userData = Component;
}

void FPhysXScene::RemoveBodyForComponent(USceneComponent* Component)
{
    if (!Component || !Impl)
    {
        return;
    }

    for (auto It = Impl->Actors.begin(); It != Impl->Actors.end();)
    {
        physx::PxRigidActor* Actor = *It;

        if (Actor && Actor->userData == Component)
        {
            if (Impl->LastDynamicBody == Actor)
            {
                Impl->LastDynamicBody = nullptr;
            }

            Actor->release();
            It = Impl->Actors.erase(It);
        }
        else
        {
            ++It;
        }
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
        USceneComponent* Component = static_cast<USceneComponent*>(Body->userData);

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
    Impl->LastDynamicBody = Actor;

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

