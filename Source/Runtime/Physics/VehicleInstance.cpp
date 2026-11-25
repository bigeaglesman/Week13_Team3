#include "EnginePCH.h"
#include "Physics/VehicleInstance.h"

#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSceneLock.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/BodyInstance.h"

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

bool FVehicleInstance::InitVehicle(FPhysicsScene* InPhysicsScene, const FTransform& Transform, bool bNotifyRigidBodyCollision)
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

    ChassisShape->setSimulationFilterData(PxFilterData(0u, bNotifyRigidBodyCollision ? FBodyInstance::FilterFlag_NotifyHit : 0u, 0u, 0u));

    // 차체 질량과 관성 텐서
    PxVehicleChassisData ChassisData;
    ChassisData.mMass = 1200.0f;
    ChassisData.mMOI = PxVec3(450.0f, 1345.0f, 1552.0f);
    ChassisData.mCMOffset = PxVec3(0.0f, 0.0f, -0.25f);

    NewChassis->setMass(ChassisData.mMass);
    NewChassis->setMassSpaceInertiaTensor(ChassisData.mMOI);
    NewChassis->setCMassLocalPose(PxTransform(ChassisData.mCMOffset));
    NewChassis->setAngularDamping(0.7f);
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
        WheelData.mDampingRate = 0.25f;
        WheelData.mMaxSteer = (i < 2) ? 0.55f : 0.0f;
        WheelData.mMaxBrakeTorque = (i < 2) ? 1800.0f : 1400.0f;
        WheelData.mMaxHandBrakeTorque = (i >= 2) ? 900.0f : 0.0f;
        WheelsSimData->setWheelData(i, WheelData);

        PxVehicleSuspensionData SuspensionData = WheelsSimData->getSuspensionData(i);

        const PxReal RideFrequency = (i < 2) ? 1.7f : 1.9f;
        const PxReal DampingRatio = 0.90f;
        const PxReal NaturalAngularFrequency = 6.28318530718f * RideFrequency;

        SuspensionData.mSprungMass = SprungMasses[i];
        SuspensionData.mMaxCompression = 0.25f;
        SuspensionData.mMaxDroop = 9.81f / (NaturalAngularFrequency * NaturalAngularFrequency);
        SuspensionData.mSpringStrength = SuspensionData.mSprungMass * NaturalAngularFrequency * NaturalAngularFrequency;
        SuspensionData.mSpringDamperRate = 2.0f * DampingRatio * PxSqrt(SuspensionData.mSpringStrength * SuspensionData.mSprungMass);

        WheelsSimData->setSuspensionData(i, SuspensionData);

        HTR_LOG(Info, "[VehicleSuspension] Wheel={} Mass={}kg Frequency={}Hz Damping={} Spring={} Damper={} Droop={}", i, SuspensionData.mSprungMass, RideFrequency, DampingRatio, SuspensionData.mSpringStrength, SuspensionData.mSpringDamperRate, SuspensionData.mMaxDroop);

        PxVehicleTireData TireData;

        TireData.mFrictionVsSlipGraph[0][0] = 0.0f;
        TireData.mFrictionVsSlipGraph[0][1] = 1.0f;
        TireData.mFrictionVsSlipGraph[1][0] = 0.10f;
        TireData.mFrictionVsSlipGraph[1][1] = 1.15f;
        TireData.mFrictionVsSlipGraph[2][0] = 1.00f;
        TireData.mFrictionVsSlipGraph[2][1] = 0.92f;

        WheelsSimData->setTireData(i, TireData);

        const PxVec3 WheelCenterCOMOffset = WheelCenterActorOffsets[i] - ChassisData.mCMOffset;
        WheelsSimData->setWheelCentreOffset(i, WheelCenterCOMOffset);
        WheelsSimData->setSuspTravelDirection(i, PxVec3(0.0f, 0.0f, -1.0f));
        WheelsSimData->setSuspForceAppPointOffset(i, WheelCenterCOMOffset);
        WheelsSimData->setTireForceAppPointOffset(i, WheelCenterCOMOffset);

        // 아직 Actor에 바퀴 Shape가 없으므로 명시적으로 연결을 끊는다.
        WheelsSimData->setWheelShapeMapping(i, -1);
        WheelsSimData->setSceneQueryFilterData(i, PxFilterData(VehicleDrivableSurfaceQueryFlag, 0, 0, 0));
    }

    // 좌우 바퀴의 서스펜션 압축량 차이를 억제하는 안티롤 바
    PxVehicleAntiRollBarData FrontAntiRollBar;
    FrontAntiRollBar.mWheel0 = 0;
    FrontAntiRollBar.mWheel1 = 1;
    FrontAntiRollBar.mStiffness = 18000.0f;
    WheelsSimData->addAntiRollBarData(FrontAntiRollBar);

    PxVehicleAntiRollBarData RearAntiRollBar;
    RearAntiRollBar.mWheel0 = 2;
    RearAntiRollBar.mWheel1 = 3;
    RearAntiRollBar.mStiffness = 16000.0f;
    WheelsSimData->addAntiRollBarData(RearAntiRollBar);

    // 4. 4륜 구동 데이터의 기본 설정
    PxVehicleDriveSimData4W DriveData;

    PxVehicleEngineData EngineData;
    EngineData.mMOI = 1.0f;
    EngineData.mPeakTorque = 420.0f;
    EngineData.mMaxOmega = 650.0f;
    EngineData.mDampingRateFullThrottle = 0.15f;
    EngineData.mDampingRateZeroThrottleClutchEngaged = 2.0f;
    EngineData.mDampingRateZeroThrottleClutchDisengaged = 0.35f;

    // 엔진 회전 속도에 따른 정규화 토크 커브
    EngineData.mTorqueCurve.clear();
    EngineData.mTorqueCurve.addPair(0.00f, 0.55f);
    EngineData.mTorqueCurve.addPair(0.15f, 0.68f);
    EngineData.mTorqueCurve.addPair(0.35f, 0.84f);
    EngineData.mTorqueCurve.addPair(0.55f, 0.95f);
    EngineData.mTorqueCurve.addPair(0.72f, 1.00f);
    EngineData.mTorqueCurve.addPair(0.88f, 0.96f);
    EngineData.mTorqueCurve.addPair(1.00f, 0.88f);

    HTR_LOG(Info, "[VehicleEngine] PeakTorque={}Nm | MaxRPM={} | CurveEntries={}", EngineData.mPeakTorque, EngineData.mMaxOmega * 60.0f / (2.0f * 3.1415926535f), EngineData.mTorqueCurve.getNbDataPairs());

    PxVehicleGearsData GearsData;
    GearsData.mFinalRatio = 3.7f;
    GearsData.mNbRatios = 8;
    GearsData.mSwitchTime = 0.2f;
    GearsData.setGearRatio(PxVehicleGearsData::eREVERSE, -3.2f);
    GearsData.setGearRatio(PxVehicleGearsData::eNEUTRAL, 0.0f);
    GearsData.setGearRatio(PxVehicleGearsData::eFIRST, 3.6f);
    GearsData.setGearRatio(PxVehicleGearsData::eSECOND, 2.65f);
    GearsData.setGearRatio(PxVehicleGearsData::eTHIRD, 1.95f);
    GearsData.setGearRatio(PxVehicleGearsData::eFOURTH, 1.43f);
    GearsData.setGearRatio(PxVehicleGearsData::eFIFTH, 1.05f);
    GearsData.setGearRatio(PxVehicleGearsData::eSIXTH, 0.82f);

    PxVehicleClutchData ClutchData;
    ClutchData.mStrength = 10.0f;
    ClutchData.mAccuracyMode = PxVehicleClutchAccuracyMode::eBEST_POSSIBLE;

    PxVehicleAutoBoxData AutoBoxData;
    AutoBoxData.setUpRatios(PxVehicleGearsData::eFIRST, 0.94f);
    AutoBoxData.setUpRatios(PxVehicleGearsData::eSECOND, 0.94f);
    AutoBoxData.setUpRatios(PxVehicleGearsData::eTHIRD, 0.94f);
    AutoBoxData.setUpRatios(PxVehicleGearsData::eFOURTH, 0.94f);
    AutoBoxData.setUpRatios(PxVehicleGearsData::eFIFTH, 0.94f);
    AutoBoxData.setDownRatios(PxVehicleGearsData::eSECOND, 0.58f);
    AutoBoxData.setDownRatios(PxVehicleGearsData::eTHIRD, 0.58f);
    AutoBoxData.setDownRatios(PxVehicleGearsData::eFOURTH, 0.58f);
    AutoBoxData.setDownRatios(PxVehicleGearsData::eFIFTH, 0.58f);
    AutoBoxData.setDownRatios(PxVehicleGearsData::eSIXTH, 0.58f);
    AutoBoxData.setLatency(0.35f);

    DriveData.setEngineData(EngineData);
    DriveData.setGearsData(GearsData);
    DriveData.setClutchData(ClutchData);
    DriveData.setAutoBoxData(AutoBoxData);

    PxVehicleDifferential4WData DifferentialData;
    DifferentialData.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
    DifferentialData.mFrontRearSplit = 0.55f;
    DifferentialData.mFrontLeftRightSplit = 0.5f;
    DifferentialData.mRearLeftRightSplit = 0.5f;
    DifferentialData.mCentreBias = 1.3f;
    DifferentialData.mFrontBias = 1.3f;
    DifferentialData.mRearBias = 1.3f;
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

        NewVehicle->setToRestState();
        NewVehicle->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
        NewVehicle->mDriveDynData.setUseAutoGears(true);
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

physx::PxVehicleWheels* FVehicleInstance::GetVehicle() const
{
    return Vehicle;
}
