#include "EnginePCH.h"
#include "PhysicsCore.h"
#include "Physics/PhysicalMaterial.h"

#include <PxPhysicsAPI.h>

using namespace physx;

// PhysicsCore.h에서 extern으로 선언한 전역들의 실제 정의
PxFoundation* GPhysXFoundation = nullptr;
PxPhysics* GPhysXSDK = nullptr;
PxPvd* GPhysXVisualDebugger = nullptr;
PxDefaultCpuDispatcher* GPhysXCpuDispatcher = nullptr;
PxMaterial* GPhysXDefaultMaterial = nullptr;

namespace
{
	// Foundation이 살아 있는 동안 계속 살아 있어야 하므로 파일 범위 static으로 둔다.
	PxDefaultAllocator GPhysXAllocator;

	// PhysX가 보내는 경고·오류를 엔진 로그로 넘긴다.
	class FPhysXErrorCallback : public PxErrorCallback
	{
	public:
		void reportError(PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			const char* SafeMessage = Message ? Message : "(no message)";
			const char* SafeFile = File ? File : "(unknown)";

			switch (Code)
			{
			case PxErrorCode::eDEBUG_INFO:
				HTR_LOG(Info, "[PhysX] {} ({}:{})", SafeMessage, SafeFile, Line);
				break;

			case PxErrorCode::eDEBUG_WARNING:
			case PxErrorCode::ePERF_WARNING:
				HTR_LOG(Warning, "[PhysX] {} ({}:{})", SafeMessage, SafeFile, Line);
				break;

			default:
				// eINVALID_PARAMETER, eINVALID_OPERATION, eOUT_OF_MEMORY, eINTERNAL_ERROR, eABORT
				HTR_LOG(Error, "[PhysX] code={} {} ({}:{})", static_cast<int>(Code), SafeMessage, SafeFile, Line);
				break;
			}
		}
	};

	FPhysXErrorCallback GPhysXErrorCallback;

	// PxInitExtensions가 성공했는지. TermGamePhys에서 PxCloseExtensions를 부를지 정한다.
	bool bPhysXExtensionsInitialized = false;
}

bool InitGamePhys()
{
	// 두 번 부르면 PxCreateFoundation이 실패한다. 이미 만들었으면 그대로 쓴다.
	if (GPhysXFoundation)
	{
		return true;
	}

	// 1. Foundation: 할당자와 에러 콜백 등록
	GPhysXFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
	if (!GPhysXFoundation)
	{
		HTR_LOG(Error, "[PhysX] PxCreateFoundation failed");
		return false;
	}

	// 2. PVD: 연결에 실패해도 엔진은 계속 돈다 (PVD 프로그램이 꺼져 있는 경우가 보통)
	GPhysXVisualDebugger = PxCreatePvd(*GPhysXFoundation);
	if (GPhysXVisualDebugger)
	{
		PxPvdTransport* Transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
		if (Transport && GPhysXVisualDebugger->connect(*Transport, PxPvdInstrumentationFlag::eDEBUG))
		{
			HTR_LOG(Info, "[PhysX] PVD connected (127.0.0.1:5425)");
		}
		else
		{
			HTR_LOG(Info, "[PhysX] PVD not connected (PVD is not running)");
		}
	}

	// 3. Physics: 모든 PhysX 객체를 만드는 공장
	GPhysXSDK = PxCreatePhysics(PX_PHYSICS_VERSION, *GPhysXFoundation, PxTolerancesScale(),
		/*trackOutstandingAllocations*/ true, GPhysXVisualDebugger);
	if (!GPhysXSDK)
	{
		HTR_LOG(Error, "[PhysX] PxCreatePhysics failed");
		TermGamePhys();
		return false;
	}

	// 4. Extensions: PxRigidBodyExt(질량 계산), 조인트 등에 필요
	if (!PxInitExtensions(*GPhysXSDK, GPhysXVisualDebugger))
	{
		HTR_LOG(Error, "[PhysX] PxInitExtensions failed");
		TermGamePhys();
		return false;
	}
	bPhysXExtensionsInitialized = true;

	// 5. CPU Dispatcher: simulate 작업을 나눠 돌릴 워커 스레드
	// 엔진도 FTaskScheduler·FTaskPool 스레드를 쓰므로 PhysX에는 적게 준다.
	constexpr uint32 NumPhysXThreads = 2;
	GPhysXCpuDispatcher = PxDefaultCpuDispatcherCreate(NumPhysXThreads);
	if (!GPhysXCpuDispatcher)
	{
		HTR_LOG(Error, "[PhysX] PxDefaultCpuDispatcherCreate failed");
		TermGamePhys();
		return false;
	}

	// 6. 기본 Material: 정지마찰, 운동마찰, 반발계수
	GPhysXDefaultMaterial = GPhysXSDK->createMaterial(0.5f, 0.5f, 0.1f);
	if (!GPhysXDefaultMaterial)
	{
		HTR_LOG(Error, "[PhysX] default material creation failed");
		TermGamePhys();
		return false;
	}

	HTR_LOG(Info, "[PhysX] initialized (SDK {}.{}.{})", PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX);
	return true;
}


void TermGamePhys()
{
	// InitGamePhys에서 만든 정확히 역순으로 해제한다.
	// 중간 실패 시에도 불리므로 모든 포인터/플래그를 검사하고 초기화한다.

	UPhysicalMaterial::ReleaseAllPhysXMaterials();

	if (GPhysXDefaultMaterial)
	{
		GPhysXDefaultMaterial->release();
		GPhysXDefaultMaterial = nullptr;
	}

	if (GPhysXCpuDispatcher)
	{
		GPhysXCpuDispatcher->release();
		GPhysXCpuDispatcher = nullptr;
	}

	if (bPhysXExtensionsInitialized)
	{
		PxCloseExtensions();
		bPhysXExtensionsInitialized = false;
	}

	if (GPhysXSDK)
	{
		GPhysXSDK->release();
		GPhysXSDK = nullptr;
	}

	if (GPhysXVisualDebugger)
	{
		PxPvdTransport* Transport = GPhysXVisualDebugger->getTransport();
		GPhysXVisualDebugger->release();
		GPhysXVisualDebugger = nullptr;

		if (Transport)
		{
			Transport->release();
		}
	}

	if (GPhysXFoundation)
	{
		GPhysXFoundation->release();
		GPhysXFoundation = nullptr;
	}
}