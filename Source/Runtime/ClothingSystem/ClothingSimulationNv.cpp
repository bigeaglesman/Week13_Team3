#include "EnginePCH.h"
#include "ClothingSystem/ClothingSimulationNv.h"
#include "ClothingSystem/ClothingSystemRuntimeModuleNv.h"
#include "Components/SkeletalMeshComponent.h"

#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>

FClothingSimulationNv::~FClothingSimulationNv()
{
	Shutdown();
}

bool FClothingSimulationNv::Initialize()
{
	if (IsInitialized())
	{
		return true;
	}

	nv::cloth::Factory* Factory = FClothingSystemRuntimeModuleNv::Get().GetSoftwareFactory();
	if (!Factory)
	{
		return false;
	}

	Solver = Factory->createSolver();
	bFirstFrame = true;
	return Solver != nullptr;
}

void FClothingSimulationNv::Shutdown()
{
	// Solver에 붙은 Cloth는 각 FClothingActorNv가 먼저 removeCloth하고 지운다. (4단계)
	// NvCloth가 먼저 내려갔으면 (프로그램 종료 중 늦게 지워진 컴포넌트) Solver는 이미 쓸 수 없으니 버린다.
	if (Solver && FClothingSystemRuntimeModuleNv::Get().IsInitialized())
	{
		NV_CLOTH_DELETE(Solver);
	}
	Solver = nullptr;
}

void FClothingSimulationNv::FillContext(const USkeletalMeshComponent* Component, float DeltaTime, FClothingSimulationContextNv& OutContext) const
{
	OutContext.DeltaSeconds = std::min(DeltaTime, MaxDeltaSeconds);
	OutContext.ComponentToWorld = Component->GetWorldMatrix();
	OutContext.BoneTransforms = Component->GetComponentSpaceTransform();
	OutContext.bTeleport = bFirstFrame;
}

void FClothingSimulationNv::Simulate(const FClothingSimulationContextNv& Context)
{
	if (!Solver || Solver->getNumCloths() == 0 || Context.DeltaSeconds <= 0.0f)
	{
		return;
	}

	// TODO : 여기서 천마다 고정 입자를 뼈에 맞추고, 중력,바람을 컴포넌트 공간으로 돌려 넣는다.

	// NvCloth는 일을 chunk로 나눠 준다. 지금은 한 스레드에서 차례로 돌린다.
	// (나중에 chunk마다 작업 스레드에 나눠 주면 그대로 병렬이 된다)
	if (Solver->beginSimulation(Context.DeltaSeconds))
	{
		const int ChunkCount = Solver->getSimulationChunkCount();
		for (int ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			Solver->simulateChunk(ChunkIndex);
		}
		Solver->endSimulation();
	}

	bFirstFrame = false;
}
