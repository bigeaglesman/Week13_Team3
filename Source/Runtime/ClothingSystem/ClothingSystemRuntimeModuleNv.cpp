#include "EnginePCH.h"
#include "ClothingSystem/ClothingSystemRuntimeModuleNv.h"

#include <malloc.h>
#include <foundation/PxAllocatorCallback.h>
#include <foundation/PxErrorCallback.h>
#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>

using namespace physx;

namespace
{
	// NvCloth는 SIMD 연산을 하므로 모든 메모리가 16바이트 정렬이어야 한다.
	class FNvClothAllocator : public PxAllocatorCallback
	{
	public:
		void* allocate(size_t Size, const char* TypeName, const char* Filename, int Line) override
		{
			return _aligned_malloc(Size, 16);
		}

		void deallocate(void* Ptr) override
		{
			_aligned_free(Ptr);
		}
	};

	class FNvClothErrorCallback : public PxErrorCallback
	{
	public:
		void reportError(PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			if (Code == PxErrorCode::eDEBUG_INFO)
			{
				HTR_LOG(Info, "[NvCloth] {} ({}:{})", Message, File, Line);
			}
			else if (Code == PxErrorCode::eDEBUG_WARNING || Code == PxErrorCode::ePERF_WARNING)
			{
				HTR_LOG(Warning, "[NvCloth] {} ({}:{})", Message, File, Line);
			}
			else
			{
				HTR_LOG(Error, "[NvCloth] {} ({}:{})", Message, File, Line);
			}
		}
	};

	// Debug lib(PX_CHECKED=1)에서만 불린다. 같은 assert가 매 프레임 쌓이지 않게 한 번 찍고 무시한다.
	class FNvClothAssertHandler : public PxAssertHandler
	{
	public:
		void operator()(const char* Expression, const char* File, int Line, bool& bIgnore) override
		{
			HTR_LOG(Error, "[NvCloth] Assert failed: {} ({}:{})", Expression, File, Line);
			bIgnore = true;
		}
	};

	FNvClothAllocator     GNvClothAllocator;
	FNvClothErrorCallback GNvClothErrorCallback;
	FNvClothAssertHandler GNvClothAssertHandler;
}

FClothingSystemRuntimeModuleNv& FClothingSystemRuntimeModuleNv::Get()
{
	static FClothingSystemRuntimeModuleNv Instance;
	return Instance;
}

bool FClothingSystemRuntimeModuleNv::StartupModule()
{
	if (IsInitialized())
	{
		return true;
	}

	nv::cloth::InitializeNvCloth(&GNvClothAllocator, &GNvClothErrorCallback, &GNvClothAssertHandler, nullptr);

	SoftwareFactory = NvClothCreateFactoryCPU();
	// CUDA를 넣게 되면 SoftwareFactory = NvClothCreateFactoryCPU(); 추가
	return SoftwareFactory != nullptr;
}

void FClothingSystemRuntimeModuleNv::ShutdownModule()
{
	// Factory로 만든 Solver, Cloth, Fabric은 이미 모두 지워진 상태여야 한다.
	if (SoftwareFactory)
	{
		NvClothDestroyFactory(SoftwareFactory);
		SoftwareFactory = nullptr;
	}
}
