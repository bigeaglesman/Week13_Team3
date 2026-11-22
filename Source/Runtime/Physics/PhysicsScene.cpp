#include "EnginePCH.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXConversion.h"
#include "Physics/BodyInstance.h"
#include "Render/LineBatcher.h"
#include "Core/Stats/LightweightStats.h"

#include <PxPhysicsAPI.h>

using namespace physx;

DECLARE_CYCLE_STAT("Physics: Simulate", STAT_PhysicsSimulate);
DECLARE_CYCLE_STAT("Physics: FetchResults", STAT_PhysicsFetchResults);
DECLARE_CYCLE_STAT("Physics: Sync Bodies", STAT_PhysicsSyncBodies);

namespace
{
	// PhysX 디버그 색(0xAARRGGBB) → 선 색
	FVector4 ToLineColor(PxU32 Color)
	{
		return FVector4(((Color >> 16) & 0xFF) / 255.0f, ((Color >> 8) & 0xFF) / 255.0f, (Color & 0xFF) / 255.0f, 1.0f);
	}
}

FPhysicsScene::FPhysicsScene(UWorld* InOwner)
	: Owner(InOwner)
{
	FPhysicsCore& Core = FPhysicsCore::Get();
	if (!Core.IsInitialized())
	{
		return;
	}

	PxPhysics* Physics = Core.GetPhysics();

	PxSceneDesc Desc(Physics->getTolerancesScale());
	Desc.gravity       = PxVec3(0.0f, 0.0f, -9.81f);   // Z-up, m/s²
	Desc.cpuDispatcher = Core.GetDispatcher();
	Desc.filterShader  = PxDefaultSimulationFilterShader;   // 4단계에서 엔진 필터 셰이더로 교체
	Desc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;        // 움직인 액터만 동기화 (2단계)
	Desc.flags |= PxSceneFlag::eENABLE_CCD;
	Desc.flags |= PxSceneFlag::eENABLE_PCM;

	Scene = Physics->createScene(Desc);
}

FPhysicsScene::~FPhysicsScene()
{
	if (Scene)
	{
		Scene->release();
		Scene = nullptr;
	}
}

void FPhysicsScene::Tick(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f)
	{
		return;
	}

	Accumulator += DeltaTime;

	int Steps = 0;
	while (Accumulator >= FixedStep && Steps < MaxSubSteps)
	{
		StepOnce(FixedStep);
		Accumulator -= FixedStep;
		++Steps;
	}

	// 한 프레임에 MaxSubSteps를 다 써도 밀려 있으면 버린다.
	// 따라잡으려고 더 돌면 프레임이 더 느려지고 다음엔 더 밀리는 악순환이 된다.
	if (Accumulator >= FixedStep)
	{
		Accumulator = 0.0f;
	}

	if (Steps > 0)
	{
		SyncActiveBodiesToComponents();
	}
}

void FPhysicsScene::SyncActiveBodiesToComponents()
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsSyncBodies);

	// 마지막 simulate에서 움직인 액터만 온다 (eENABLE_ACTIVE_ACTORS). 잠든 바디와 바닥은 빠진다.
	PxU32 NumActive = 0;
	PxActor** ActiveActors = Scene->getActiveActors(NumActive);
	for (PxU32 i = 0; i < NumActive; ++i)
	{
		if (FBodyInstance* Body = static_cast<FBodyInstance*>(ActiveActors[i]->userData))
		{
			Body->SyncComponentToBody();
		}
	}
}

void FPhysicsScene::StepOnce(float Step)
{
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysicsSimulate);
		Scene->simulate(Step);
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysicsFetchResults);
		Scene->fetchResults(true);
	}
}

void FPhysicsScene::SetDebugVisualization(bool bEnable)
{
	if (!Scene || bDebugVisualization == bEnable)
	{
		return;
	}
	bDebugVisualization = bEnable;

	// eSCALE이 0이면 PhysX가 렌더 버퍼를 아예 채우지 않는다. 꺼 두면 비용이 없다.
	const float On = bEnable ? 1.0f : 0.0f;
	Scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, On);
	Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, On);
}

void FPhysicsScene::DrawDebug(FLineBatcher& LineBatcher) const
{
	if (!Scene || !bDebugVisualization)
	{
		return;
	}

	// 마지막 fetchResults 시점의 도형·조인트 선
	const PxRenderBuffer& Buffer = Scene->getRenderBuffer();

	const PxDebugLine* Lines = Buffer.getLines();
	for (PxU32 i = 0; i < Buffer.getNbLines(); ++i)
	{
		LineBatcher.AddLine(ToFVector(Lines[i].pos0), ToFVector(Lines[i].pos1), ToLineColor(Lines[i].color0));
	}

	const PxDebugTriangle* Tris = Buffer.getTriangles();
	for (PxU32 i = 0; i < Buffer.getNbTriangles(); ++i)
	{
		const FVector A = ToFVector(Tris[i].pos0);
		const FVector B = ToFVector(Tris[i].pos1);
		const FVector C = ToFVector(Tris[i].pos2);
		const FVector4 Color = ToLineColor(Tris[i].color0);
		LineBatcher.AddLine(A, B, Color);
		LineBatcher.AddLine(B, C, Color);
		LineBatcher.AddLine(C, A, Color);
	}
}
