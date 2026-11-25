#pragma once

#include "Containers/Array.h"
#include "Math/Vector.h"

class UWorld;
class USceneComponent;
class FLineBatcher;
struct FBodyInstance;
class FPhysicsSimulationEventCallback;
class UPrimitiveComponent;

namespace physx
{
	class PxScene;
	class PxVehicleWheels;
	class PxRigidActor;
	class PxMaterial;
}

enum class EVehicleSurfaceType : uint8;

struct FVehicleSceneQueryData;

// simulate 도중 PhysX가 알려 준 충돌 하나. 시뮬레이션이 끝난 뒤 게임 스레드에서 보낸다.
// UE의 FCollisionNotifyInfo 자리다.
struct FCollisionNotifyInfo
{
	FBodyInstance* BodyA = nullptr;   // 바디가 지워지면 nullptr로 바뀐다
	FBodyInstance* BodyB = nullptr;
	UPrimitiveComponent* ComponentA = nullptr;
	UPrimitiveComponent* ComponentB = nullptr;
	bool bNotifyA = false;            // A 쪽 컴포넌트에 OnComponentHit을 보낼지 (A의 bNotifyRigidBodyCollision)
	bool bNotifyB = false;
	FVector ImpactPoint = FVector(0, 0, 0);
	FVector ImpactNormal = FVector(0, 0, 0);   // B에서 A 쪽을 향한다
	FVector NormalImpulse = FVector(0, 0, 0);
};

struct FVehicleWheelState
{
	bool bIsInAir = true;
	bool bHasContact = false;

	FVector SuspensionLineStart = FVector(0.0f, 0.0f, 0.0f);
	FVector SuspensionLineDirection = FVector(0.0f, 0.0f, 0.0f);
	float SuspensionLineLength = 0.0f;

	FVector WheelCenterOffset = FVector(0.0f, 0.0f, 0.0f);
	FVector SuspensionTravelDirection = FVector(0.0f, 0.0f, 0.0f);

	FVector TireContactPoint = FVector(0.0f, 0.0f, 0.0f);
	FVector TireContactNormal = FVector(0.0f, 0.0f, 0.0f);
	FVector TireLongitudinalDirection = FVector(0.0f, 0.0f, 0.0f);
	FVector TireLateralDirection = FVector(0.0f, 0.0f, 0.0f);

	float WheelRadius = 0.0f;
	float WheelWidth = 0.0f;

	float SuspensionJounce = 0.0f;
	float SuspensionTotalForce = 0.0f;
	float SuspensionJounceVelocityEstimate = 0.0f;
	float SuspensionSpringComponent = 0.0f;
	float SuspensionDamperComponent = 0.0f;

	float TireFriction = 0.0f;
	float LongitudinalSlip = 0.0f;
	float LateralSlip = 0.0f;

	float SteerAngle = 0.0f;
	float WheelRotationAngle = 0.0f;

	int32 TireSurfaceType = -1;
};

enum class EVehicleDriveMode : uint8
{
	AWDLSD = 0,
	AWDOpen,
	RWDLSD
};

struct FVehicleDriveState
{
	float ForwardSpeed = 0.0f;
	float LinearSpeed = 0.0f;

	FVector LocalLinearVelocity = FVector(0.0f, 0.0f, 0.0f);
	FVector LocalAngularVelocity = FVector(0.0f, 0.0f, 0.0f);

	float EngineRPM = 0.0f;

	int32 CurrentGear = 0;
	int32 TargetGear = 0;

	bool bUseAutoGears = false;
};

// 월드 하나당 PxScene 하나. 고정 스텝으로 시뮬레이션한다.
// UE 4.27의 FPhysScene_PhysX 자리다.
//
// 멀티스레드: PxScene은 eREQUIRE_RW_LOCK으로 만든다. simulate는 PhysX 작업 스레드(PxDefaultCpuDispatcher)가 나눠 돌리고,
// 씬에 들어간 객체를 엔진 쪽에서 만질 때는 PhysXSceneLock.h의 SCOPED_SCENE_READ/WRITE_LOCK을 건다.
class FPhysicsScene
{
public:
	explicit FPhysicsScene(UWorld* InOwner);
	~FPhysicsScene();

	FPhysicsScene(const FPhysicsScene&) = delete;
	FPhysicsScene& operator=(const FPhysicsScene&) = delete;

	bool IsValid() const { return Scene != nullptr; }

	// GameWorldTick에서 PrePhysics 틱 그룹 다음에 부른다.
	void Tick(float DeltaTime);

	// 디버깅용 Physic Body 렌더링. Collision 쇼 플래그가 켜진 뷰포트가 매 프레임 요청하고 DrawDebug로 그린다.
	// 여러 뷰포트(PIE 게임 뷰포트, 레벨 에디터 뷰포트)가 같은 씬을 보므로, 한 곳이라도 요청하면 다음 스텝에 PhysX가 선을 채운다.
	// 아무도 요청하지 않으면 다음 Tick에 꺼서 비용을 없앤다.
	enum class EDebugVisualization { Off, Shapes, ShapesAndJoints };
	void RequestDebugVisualization(EDebugVisualization Mode = EDebugVisualization::ShapesAndJoints)
	{
		if (Mode > RequestedVisualization) RequestedVisualization = Mode;
	}
	void DrawDebug(FLineBatcher& LineBatcher) const;

	physx::PxScene* GetPxScene() const { return Scene; }
	UWorld* GetOwner() const { return Owner; }

	// ---- 충돌 이벤트 ----
	// PhysX 콜백(fetchResults 안)에서 부른다. 바로 보내지 않고 모아 둔다.
	void AddPendingCollisionNotify(const FCollisionNotifyInfo& Info);
	// 바디가 지워질 때 부른다. 모아 둔 이벤트에서 이 바디를 지운다.
	void RemovePendingNotifies(const FBodyInstance* Body);

	bool AddVehicle(physx::PxVehicleWheels* Vehicle, USceneComponent* SyncComponent);
	void RemoveVehicle(physx::PxVehicleWheels* Vehicle);

	void SetVehicleInput(physx::PxVehicleWheels* Vehicle, bool bAccelerate, bool bBrake, bool bHandbrake, bool bSteerLeft, bool bSteerRight);
	bool GetVehicleWheelStates(physx::PxVehicleWheels* Vehicle, FVehicleWheelState* OutWheelStates, int32 MaxWheelCount, bool& OutIsInAir) const;
	bool GetVehicleDriveState(physx::PxVehicleWheels* Vehicle, FVehicleDriveState& OutState) const;
	bool SetVehicleGear(physx::PxVehicleWheels* Vehicle, int32 Gear, bool bUseAutoGears, bool bForce);
	bool SetVehicleAutomaticGears(physx::PxVehicleWheels* Vehicle, bool bUseAutoGears);
	bool SetVehicleDriveMode(physx::PxVehicleWheels* Vehicle, EVehicleDriveMode Mode);
	bool RefreshVehicleFrictionPairs();
	physx::PxMaterial* GetVehicleSurfaceMaterial(EVehicleSurfaceType SurfaceType) const;

	static constexpr float FixedStep = 1.0f / 60.0f;
	static constexpr int   MaxSubSteps = 4;   // 이보다 밀리면 남은 시간은 버린다

private:
	friend class FPhysicsSimulationEventCallback;

	UPrimitiveComponent* FindVehicleComponent(const physx::PxRigidActor* Actor) const;

	void SetDebugVisualization(EDebugVisualization Mode);
	void StepOnce(float Step);

	// 시뮬레이션 결과(움직인 바디의 위치·회전)를 컴포넌트에 되돌려 쓴다. UE의 SyncComponentsToBodies 자리
	void SyncActiveBodiesToComponents();
	void SyncVehiclesToComponents(float Alpha);

	// 모아 둔 충돌 이벤트를 컴포넌트에 보낸다. 씬 락 밖에서 부른다 (받는 쪽이 바디를 만들거나 지울 수 있다).
	// UE의 FPhysScene_PhysX::DispatchPhysNotifications_AssumesLocked 자리
	void DispatchPhysNotifications();

	UWorld*         Owner = nullptr;
	physx::PxScene* Scene = nullptr;
	float           Accumulator = 0.0f;
	EDebugVisualization DebugVisualization = EDebugVisualization::Off;
	EDebugVisualization RequestedVisualization = EDebugVisualization::Off;

	FPhysicsSimulationEventCallback* EventCallback = nullptr;
	TArray<FCollisionNotifyInfo>     PendingCollisionNotifies;

	FVehicleSceneQueryData* VehicleSceneQueryData = nullptr;
};
