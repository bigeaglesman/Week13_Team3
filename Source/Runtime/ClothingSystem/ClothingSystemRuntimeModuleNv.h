#pragma once

// 프로세스에 하나뿐인 NvCloth 전역 상태 (라이브러리 초기화 + CPU Factory).
// UE 4.27 ClothingSystemRuntimeNv 플러그인의 FClothingSystemRuntimeModuleNv 자리다.
// 컴포넌트별 시뮬레이션(FClothingSimulationNv)은 여기서 Factory를 받아 Solver와 Cloth를 만든다.
namespace nv
{
	namespace cloth
	{
		class Factory;
	}
}

class FClothingSystemRuntimeModuleNv
{
public:
	static FClothingSystemRuntimeModuleNv& Get();

	bool StartupModule();     // UEngine::Init (FPhysicsCore 다음)
	void ShutdownModule();    // FEngineLoop::Exit (모든 천 시뮬레이션이 사라진 뒤)

	bool IsInitialized() const { return SoftwareFactory != nullptr; }

	nv::cloth::Factory* GetSoftwareFactory() const { return SoftwareFactory; }

private:
	FClothingSystemRuntimeModuleNv() = default;
	FClothingSystemRuntimeModuleNv(const FClothingSystemRuntimeModuleNv&) = delete;
	FClothingSystemRuntimeModuleNv& operator=(const FClothingSystemRuntimeModuleNv&) = delete;

	nv::cloth::Factory* SoftwareFactory = nullptr;   // CPU 솔버용 Factory
};
