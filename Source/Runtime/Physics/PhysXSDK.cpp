#include "EnginePCH.h"
#include "Physics/PhysXSDK.h"

using namespace physx;

namespace
{
	PxDefaultAllocator Allocator;
	PxDefaultErrorCallback ErrorCallback;

	PxFoundation* Foundation = nullptr;
	PxPhysics* Physics = nullptr;
	PxCooking* CookingInstance = nullptr;
	PxDefaultCpuDispatcher* CpuDispatcher = nullptr;
	PxMaterial* Material = nullptr;
}

void PhysXSDK::Init()
{
	Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, ErrorCallback);
	if (!Foundation)
	{
		HTR_LOG(Error, "[Physics] PxCreateFoundation failed");
		return;
	}

	const PxTolerancesScale Scale;   // 기본값이 미터 단위라 엔진 단위와 같다
	Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, Scale);
	PxInitExtensions(*Physics, nullptr);   // D6 조인트 등 Extensions 기능에 필요
	CookingInstance = PxCreateCooking(PX_PHYSICS_VERSION, *Foundation, PxCookingParams(Scale));
	CpuDispatcher = PxDefaultCpuDispatcherCreate(2);
	Material = Physics->createMaterial(0.5f, 0.5f, 0.3f);   // 정지 마찰, 운동 마찰, 반발 계수

	HTR_LOG(Info, "[Physics] PhysX {}.{} initialized", PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR);
}

// 생성 역순으로 해제한다. 모든 PxScene이 먼저 release된 뒤에 불러야 한다.
void PhysXSDK::Term()
{
	if (CpuDispatcher) { CpuDispatcher->release(); CpuDispatcher = nullptr; }
	if (CookingInstance) { CookingInstance->release(); CookingInstance = nullptr; }
	if (Physics)
	{
		if (Material) { Material->release(); Material = nullptr; }

		PxCloseExtensions();
		Physics->release();
		Physics = nullptr;
	}
	if (Foundation) { Foundation->release(); Foundation = nullptr; }
}

PxPhysics* PhysXSDK::Get() { return Physics; }
PxCooking* PhysXSDK::Cooking() { return CookingInstance; }
PxDefaultCpuDispatcher* PhysXSDK::Dispatcher() { return CpuDispatcher; }
PxMaterial* PhysXSDK::DefaultMaterial() { return Material; }
