#pragma once

// PhysX 타입은 전방 선언만 한다. 이 헤더를 include하는 엔진 파일이
// PxPhysicsAPI.h(매우 큼)까지 끌어오지 않게 하려는 것이다.
namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxPvd;
	class PxPvdTransport;
	class PxDefaultCpuDispatcher;
	class PxMaterial;
}

// 프로세스에 하나만 있는 PhysX 전역 객체들. InitGamePhys가 만들고 TermGamePhys가 지운다.
extern physx::PxFoundation* GPhysXFoundation;
extern physx::PxPhysics* GPhysXSDK;
extern physx::PxPvd* GPhysXVisualDebugger;
extern physx::PxDefaultCpuDispatcher* GPhysXCpuDispatcher;
extern physx::PxMaterial* GPhysXDefaultMaterial;

// 엔진 시작 시 한 번. GEngine을 만들기 전에 부른다.
bool InitGamePhys();

// 엔진 종료 시 한 번. 모든 World/UObject가 정리된 뒤에 부른다.
void TermGamePhys();