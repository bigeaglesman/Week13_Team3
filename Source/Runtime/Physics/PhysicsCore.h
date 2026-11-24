#pragma once

// 프로세스에 하나뿐인 PhysX 객체들 (Foundation, Physics, Cooking, 작업 스레드, 기본 재질).
// 월드마다 하나인 PxScene은 FPhysicsScene이 갖는다.
// UE 4.27의 InitGamePhysCore / TermGamePhysCore 자리다.
namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxCooking;
	class PxPvd;
	class PxPvdTransport;
	class PxDefaultCpuDispatcher;
	class PxMaterial;
}

class FPhysicsCore
{
public:
	static FPhysicsCore& Get();

	bool Init();        // UEngine::Init
	void Shutdown();    // FEngineLoop::Exit (모든 월드가 사라진 뒤)

	bool IsInitialized() const { return Physics != nullptr; }
	bool IsVehicleSDKInitialized() const { return bVehicleSDKInitialized; }

	physx::PxPhysics*              GetPhysics() const         { return Physics; }
	physx::PxCooking*              GetCooking() const         { return Cooking; }
	physx::PxDefaultCpuDispatcher* GetDispatcher() const      { return Dispatcher; }
	physx::PxMaterial*             GetDefaultMaterial() const { return DefaultMaterial; }

private:
	FPhysicsCore() = default;
	FPhysicsCore(const FPhysicsCore&) = delete;
	FPhysicsCore& operator=(const FPhysicsCore&) = delete;

	physx::PxFoundation*           Foundation = nullptr;
	physx::PxPvd*                  Pvd = nullptr;            // PhysX Visual Debugger 연결 (127.0.0.1:5425)
	physx::PxPvdTransport*         PvdTransport = nullptr;
	physx::PxPhysics*              Physics = nullptr;
	bool                           bVehicleSDKInitialized = false;
	physx::PxCooking*              Cooking = nullptr;
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
	physx::PxMaterial*             DefaultMaterial = nullptr;
};
