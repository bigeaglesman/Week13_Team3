#include "EnginePCH.h"
#include "Physics/VehicleInstance.h"

#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSceneLock.h"

#include <PxPhysicsAPI.h>
#include <PxScene.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleUtilSetup.h>

using namespace physx;

namespace
{
    constexpr PxU32 VehicleWheelCount = 4;
}

FVehicleInstance::~FVehicleInstance()
{
    TermVehicle();
}

bool FVehicleInstance::InitVehicle(FPhysicsScene* InPhysicsScene, const FTransform& Transform)
{
    if (IsValid() || !InPhysicsScene || !InPhysicsScene->IsValid())
    {
        return false;
    }

    FPhysicsCore& Core = FPhysicsCore::Get();
    if (!Core.IsInitialized() || !Core.IsVehicleSDKInitialized())
    {
        HTR_LOG(Error, "[Vehicle] PhysX Core or Vehicle SDK is not initialized.");
        return false;
    }

    PxPhysics* Physics = Core.GetPhysics();
    PxScene* Scene = InPhysicsScene->GetPxScene();
    PxMaterial* Material = Core.GetDefaultMaterial();

    if (!Physics || !Scene || !Material)
    {
        return false;
    }

    // 1. 차체 Actor 생성
    PxRigidDynamic* NewChassis = Physics->createRigidDynamic(ToPx(Transform));
    if (!NewChassis)
    {
        return false;
    }

    // 현재 단계의 차체 충돌 형상: 길이 3.6m, 너비 1.6m, 높이 0.7m
    PxShape* ChassisShape = PxRigidActorExt::createExclusiveShape(*NewChassis, PxBoxGeometry(1.8f, 0.8f, 0.35f), *Material);
    if (!ChassisShape)
    {
        NewChassis->release();
        return false;
    }

    // 차체 질량과 관성 텐서
    PxVehicleChassisData ChassisData;
    ChassisData.mMass = 1200.0f;
    ChassisData.mMOI = PxVec3(305.0f, 1345.0f, 1552.0f);
    ChassisData.mCMOffset = PxVec3(0.0f, 0.0f, 0.0f);

    NewChassis->setMass(ChassisData.mMass);
    NewChassis->setMassSpaceInertiaTensor(ChassisData.mMOI);
    NewChassis->setCMassLocalPose(PxTransform(ChassisData.mCMOffset));
    NewChassis->userData = nullptr;

    // 2. 4개 바퀴의 기본 위치: X=전방, Y=좌우, Z=상하
    const PxVec3 WheelCenterActorOffsets[VehicleWheelCount] =
    {
        PxVec3(1.25f, -0.95f, -0.48f), // Front Left
        PxVec3(1.25f,  0.95f, -0.48f), // Front Right
        PxVec3(-1.25f, -0.95f, -0.48f), // Rear Left
        PxVec3(-1.25f,  0.95f, -0.48f)  // Rear Right
    };

    PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(VehicleWheelCount);
    if (!WheelsSimData)
    {
        NewChassis->release();
        return false;
    }

    WheelsSimData->setChassisMass(ChassisData.mMass);

    PxReal SprungMasses[VehicleWheelCount] = {};
    PxVehicleComputeSprungMasses(VehicleWheelCount, WheelCenterActorOffsets, ChassisData.mCMOffset, ChassisData.mMass, 2, SprungMasses);

    // 3. Wheel / Suspension / Tire 데이터 설정
    for (PxU32 i = 0; i < VehicleWheelCount; ++i)
    {
        PxVehicleWheelData WheelData;
        WheelData.mRadius = 0.36f;
        WheelData.mWidth = 0.24f;
        WheelData.mMass = 20.0f;
        WheelData.mMOI = 1.296f;
        WheelData.mMaxSteer = (i < 2) ? 0.55f : 0.0f;
        WheelData.mMaxBrakeTorque = 1500.0f;
        WheelData.mMaxHandBrakeTorque = (i >= 2) ? 3000.0f : 0.0f;
        WheelsSimData->setWheelData(i, WheelData);

        PxVehicleSuspensionData SuspensionData = WheelsSimData->getSuspensionData(i);
        SuspensionData.setMassAndPreserveNaturalFrequency(SprungMasses[i]);
        WheelsSimData->setSuspensionData(i, SuspensionData);

        PxVehicleTireData TireData;
        WheelsSimData->setTireData(i, TireData);

        const PxVec3 WheelCenterCOMOffset = WheelCenterActorOffsets[i] - ChassisData.mCMOffset;
        WheelsSimData->setWheelCentreOffset(i, WheelCenterCOMOffset);
        WheelsSimData->setSuspTravelDirection(i, PxVec3(0.0f, 0.0f, -1.0f));
        WheelsSimData->setSuspForceAppPointOffset(i, WheelCenterCOMOffset);
        WheelsSimData->setTireForceAppPointOffset(i, WheelCenterCOMOffset);

        // 아직 Actor에 바퀴 Shape가 없으므로 명시적으로 연결을 끊는다.
        WheelsSimData->setWheelShapeMapping(i, -1);
    }

    // 4. 4륜 구동 데이터의 기본 설정
    PxVehicleDriveSimData4W DriveData;

    PxVehicleEngineData EngineData;
    PxVehicleGearsData GearsData;
    PxVehicleClutchData ClutchData;
    PxVehicleAutoBoxData AutoBoxData;

    DriveData.setEngineData(EngineData);
    DriveData.setGearsData(GearsData);
    DriveData.setClutchData(ClutchData);
    DriveData.setAutoBoxData(AutoBoxData);

    PxVehicleDifferential4WData DifferentialData;
    DifferentialData.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
    DriveData.setDiffData(DifferentialData);

    PxVehicleAckermannGeometryData AckermannData;
    AckermannData.mFrontWidth = 1.9f;
    AckermannData.mRearWidth = 1.9f;
    AckermannData.mAxleSeparation = 2.5f;
    DriveData.setAckermannGeometryData(AckermannData);

    // 5. Wheel 데이터와 구동 데이터를 차량 인스턴스에 연결
    PxVehicleDrive4W* NewVehicle = PxVehicleDrive4W::create(Physics, NewChassis, *WheelsSimData, DriveData, 0);
    WheelsSimData->free();

    if (!NewVehicle)
    {
        NewChassis->release();
        return false;
    }

    // 6. PhysX Scene에 차체 등록
    {
        SCOPED_SCENE_WRITE_LOCK(Scene);
        Scene->addActor(*NewChassis);
    }

    ChassisActor = NewChassis;
    Vehicle = NewVehicle;

    HTR_LOG(Info, "[Vehicle] Initialized | Wheels={} | ChassisMass={}kg", VehicleWheelCount, ChassisData.mMass);
    return true;
}

void FVehicleInstance::TermVehicle()
{
    // PhysX SDK 샘플의 차량 해제 순서에 맞춰 Actor를 먼저 해제한다.
    if (ChassisActor)
    {
        PxScene* Scene = ChassisActor->getScene();

        if (Scene)
        {
            SCOPED_SCENE_WRITE_LOCK(Scene);
            ChassisActor->userData = nullptr;
            ChassisActor->release();
        }
        else
        {
            ChassisActor->userData = nullptr;
            ChassisActor->release();
        }

        ChassisActor = nullptr;
    }

    if (Vehicle)
    {
        Vehicle->free();
        Vehicle = nullptr;
    }
}
