from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent
BACKUP = ROOT / ".monstertruck_backup"


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}. No files were written.")
    return text.replace(old, new, 1)

files = {
    "Source/Runtime/Physics/VehicleInstance.h": (ROOT / "Source/Runtime/Physics/VehicleInstance.h").read_text(encoding="utf-8"),
    "Source/Runtime/Physics/VehicleInstance.cpp": (ROOT / "Source/Runtime/Physics/VehicleInstance.cpp").read_text(encoding="utf-8"),
    "Source/Runtime/Components/VehicleMovementComponent.h": (ROOT / "Source/Runtime/Components/VehicleMovementComponent.h").read_text(encoding="utf-8"),
    "Source/Runtime/Components/VehicleMovementComponent.cpp": (ROOT / "Source/Runtime/Components/VehicleMovementComponent.cpp").read_text(encoding="utf-8"),
}

files["Source/Runtime/Physics/VehicleInstance.h"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.h"],
    "bool InitVehicle(FPhysicsScene* InPhysicsScene, const FTransform& Transform, bool bNotifyRigidBodyCollision);",
    "bool InitVehicle(FPhysicsScene* InPhysicsScene, const FTransform& Transform, bool bNotifyRigidBodyCollision, bool bMonsterTruck = false);",
    "VehicleInstance.h InitVehicle declaration")

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.cpp"],
    "bool FVehicleInstance::InitVehicle(FPhysicsScene* InPhysicsScene, const FTransform& Transform, bool bNotifyRigidBodyCollision)",
    "bool FVehicleInstance::InitVehicle(FPhysicsScene* InPhysicsScene, const FTransform& Transform, bool bNotifyRigidBodyCollision, bool bMonsterTruck)",
    "VehicleInstance.cpp InitVehicle definition")

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.cpp"],
    "    // 현재 단계의 차체 충돌 형상: 길이 3.6m, 너비 1.6m, 높이 0.7m\n    PxShape* ChassisShape = PxRigidActorExt::createExclusiveShape(*NewChassis, PxBoxGeometry(1.8f, 0.8f, 0.35f), *Material);",
    "    // GT3와 몬스터 트럭은 같은 4륜 PhysX 경로를 사용하되 차체 크기만 분리한다.\n    const PxVec3 ChassisHalfExtents = bMonsterTruck ? PxVec3(1.95f, 0.95f, 0.42f) : PxVec3(1.8f, 0.8f, 0.35f);\n    PxShape* ChassisShape = PxRigidActorExt::createExclusiveShape(*NewChassis, PxBoxGeometry(ChassisHalfExtents.x, ChassisHalfExtents.y, ChassisHalfExtents.z), *Material);",
    "VehicleInstance.cpp chassis shape")

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.cpp"],
    "    ChassisData.mMass = 1200.0f;\n    ChassisData.mMOI = PxVec3(450.0f, 1345.0f, 1552.0f);\n    ChassisData.mCMOffset = PxVec3(0.0f, 0.0f, -0.25f);",
    "    ChassisData.mMass = bMonsterTruck ? 2800.0f : 1200.0f;\n    ChassisData.mMOI = bMonsterTruck ? PxVec3(920.0f, 3532.0f, 4125.0f) : PxVec3(450.0f, 1345.0f, 1552.0f);\n    ChassisData.mCMOffset = bMonsterTruck ? PxVec3(0.0f, 0.0f, -0.38f) : PxVec3(0.0f, 0.0f, -0.25f);",
    "VehicleInstance.cpp chassis mass/inertia")

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.cpp"],
    "    NewChassis->setAngularDamping(0.7f);",
    "    NewChassis->setAngularDamping(bMonsterTruck ? 0.22f : 0.7f);",
    "VehicleInstance.cpp angular damping")

old_offsets = """    const PxVec3 WheelCenterActorOffsets[VehicleWheelCount] =
    {
        PxVec3(1.25f, -0.95f, -0.48f), // Front Left
        PxVec3(1.25f,  0.95f, -0.48f), // Front Right
        PxVec3(-1.25f, -0.95f, -0.48f), // Rear Left
        PxVec3(-1.25f,  0.95f, -0.48f)  // Rear Right
    };"""
new_offsets = """    const PxVec3 WheelCenterActorOffsets[VehicleWheelCount] =
    {
        bMonsterTruck ? PxVec3(1.58f, -1.28f, -0.92f) : PxVec3(1.25f, -0.95f, -0.48f), // Front Left
        bMonsterTruck ? PxVec3(1.58f,  1.28f, -0.92f) : PxVec3(1.25f,  0.95f, -0.48f), // Front Right
        bMonsterTruck ? PxVec3(-1.58f, -1.28f, -0.92f) : PxVec3(-1.25f, -0.95f, -0.48f), // Rear Left
        bMonsterTruck ? PxVec3(-1.58f,  1.28f, -0.92f) : PxVec3(-1.25f,  0.95f, -0.48f)  // Rear Right
    };"""
files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(files["Source/Runtime/Physics/VehicleInstance.cpp"], old_offsets, new_offsets, "VehicleInstance.cpp four wheel offsets")

old_wheels = """        PxVehicleWheelData WheelData;
        WheelData.mRadius = 0.36f;
        WheelData.mWidth = 0.24f;
        WheelData.mMass = 20.0f;
        WheelData.mMOI = 1.296f;
        WheelData.mDampingRate = 0.25f;
        WheelData.mMaxSteer = (i < 2) ? 0.55f : 0.0f;
        WheelData.mMaxBrakeTorque = (i < 2) ? 1800.0f : 1400.0f;
        WheelData.mMaxHandBrakeTorque = (i >= 2) ? 900.0f : 0.0f;
        WheelsSimData->setWheelData(i, WheelData);"""
new_wheels = """        PxVehicleWheelData WheelData;
        WheelData.mRadius = bMonsterTruck ? 0.82f : 0.36f;
        WheelData.mWidth = bMonsterTruck ? 0.56f : 0.24f;
        WheelData.mMass = bMonsterTruck ? 85.0f : 20.0f;
        WheelData.mMOI = bMonsterTruck ? 28.6f : 1.296f;
        WheelData.mDampingRate = bMonsterTruck ? 0.45f : 0.25f;
        WheelData.mMaxSteer = (i < 2) ? (bMonsterTruck ? 0.42f : 0.55f) : 0.0f;
        WheelData.mMaxBrakeTorque = (i < 2) ? (bMonsterTruck ? 3400.0f : 1800.0f) : (bMonsterTruck ? 2800.0f : 1400.0f);
        WheelData.mMaxHandBrakeTorque = (i >= 2) ? (bMonsterTruck ? 1700.0f : 900.0f) : 0.0f;
        WheelsSimData->setWheelData(i, WheelData);"""
files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(files["Source/Runtime/Physics/VehicleInstance.cpp"], old_wheels, new_wheels, "VehicleInstance.cpp wheel data")

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.cpp"],
    "        const PxReal RideFrequency = (i < 2) ? 1.7f : 1.9f;\n        const PxReal DampingRatio = 0.90f;",
    "        const PxReal RideFrequency = bMonsterTruck ? ((i < 2) ? 1.05f : 1.15f) : ((i < 2) ? 1.7f : 1.9f);\n        const PxReal DampingRatio = bMonsterTruck ? 0.30f : 0.90f;",
    "VehicleInstance.cpp suspension frequency/damping")

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.cpp"],
    "        SuspensionData.mMaxCompression = 0.25f;\n        SuspensionData.mMaxDroop = 9.81f / (NaturalAngularFrequency * NaturalAngularFrequency);",
    "        SuspensionData.mMaxCompression = bMonsterTruck ? 0.62f : 0.25f;\n        SuspensionData.mMaxDroop = bMonsterTruck ? 0.55f : 9.81f / (NaturalAngularFrequency * NaturalAngularFrequency);",
    "VehicleInstance.cpp suspension travel")

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(files["Source/Runtime/Physics/VehicleInstance.cpp"], "    FrontAntiRollBar.mStiffness = 18000.0f;", "    FrontAntiRollBar.mStiffness = bMonsterTruck ? 2800.0f : 18000.0f;", "VehicleInstance.cpp front anti-roll")
files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(files["Source/Runtime/Physics/VehicleInstance.cpp"], "    RearAntiRollBar.mStiffness = 16000.0f;", "    RearAntiRollBar.mStiffness = bMonsterTruck ? 2200.0f : 16000.0f;", "VehicleInstance.cpp rear anti-roll")

for old,new,label in [
    ("    EngineData.mMOI = 1.0f;", "    EngineData.mMOI = bMonsterTruck ? 2.5f : 1.0f;", "engine MOI"),
    ("    EngineData.mPeakTorque = 420.0f;", "    EngineData.mPeakTorque = bMonsterTruck ? 950.0f : 420.0f;", "engine peak torque"),
    ("    EngineData.mMaxOmega = 650.0f;", "    EngineData.mMaxOmega = bMonsterTruck ? 540.0f : 650.0f;", "engine max omega"),
    ("    GearsData.mFinalRatio = 3.7f;", "    GearsData.mFinalRatio = bMonsterTruck ? 5.2f : 3.7f;", "final gear ratio"),
    ("    GearsData.mSwitchTime = 0.2f;", "    GearsData.mSwitchTime = bMonsterTruck ? 0.35f : 0.2f;", "gear shift time"),
    ("    GearsData.setGearRatio(PxVehicleGearsData::eFIRST, 3.6f);", "    GearsData.setGearRatio(PxVehicleGearsData::eFIRST, bMonsterTruck ? 4.2f : 3.6f);", "first gear"),
    ("    GearsData.setGearRatio(PxVehicleGearsData::eSECOND, 2.65f);", "    GearsData.setGearRatio(PxVehicleGearsData::eSECOND, bMonsterTruck ? 2.8f : 2.65f);", "second gear"),
    ("    GearsData.setGearRatio(PxVehicleGearsData::eTHIRD, 1.95f);", "    GearsData.setGearRatio(PxVehicleGearsData::eTHIRD, bMonsterTruck ? 1.9f : 1.95f);", "third gear"),
    ("    GearsData.setGearRatio(PxVehicleGearsData::eFOURTH, 1.43f);", "    GearsData.setGearRatio(PxVehicleGearsData::eFOURTH, bMonsterTruck ? 1.35f : 1.43f);", "fourth gear"),
    ("    GearsData.setGearRatio(PxVehicleGearsData::eFIFTH, 1.05f);", "    GearsData.setGearRatio(PxVehicleGearsData::eFIFTH, bMonsterTruck ? 1.0f : 1.05f);", "fifth gear"),
    ("    GearsData.setGearRatio(PxVehicleGearsData::eSIXTH, 0.82f);", "    GearsData.setGearRatio(PxVehicleGearsData::eSIXTH, bMonsterTruck ? 0.78f : 0.82f);", "sixth gear"),
    ("    ClutchData.mStrength = 10.0f;", "    ClutchData.mStrength = bMonsterTruck ? 22.0f : 10.0f;", "clutch strength"),
    ("    AckermannData.mFrontWidth = 1.9f;", "    AckermannData.mFrontWidth = bMonsterTruck ? 2.56f : 1.9f;", "front track width"),
    ("    AckermannData.mRearWidth = 1.9f;", "    AckermannData.mRearWidth = bMonsterTruck ? 2.56f : 1.9f;", "rear track width"),
    ("    AckermannData.mAxleSeparation = 2.5f;", "    AckermannData.mAxleSeparation = bMonsterTruck ? 3.16f : 2.5f;", "wheelbase"),
]:
    files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(files["Source/Runtime/Physics/VehicleInstance.cpp"], old, new, "VehicleInstance.cpp "+label)

files["Source/Runtime/Physics/VehicleInstance.cpp"] = replace_once(
    files["Source/Runtime/Physics/VehicleInstance.cpp"],
    '    HTR_LOG(Info, "[Vehicle] Initialized | Wheels={} | ChassisMass={}kg", VehicleWheelCount, ChassisData.mMass);',
    '    HTR_LOG(Info, "[Vehicle] Initialized | Type={} | Wheels={} | ChassisMass={}kg | TireRadius={}m | SuspensionTravel={}m", bMonsterTruck ? "MONSTER_TRUCK_4W" : "GT3_4W", VehicleWheelCount, ChassisData.mMass, bMonsterTruck ? 0.82f : 0.36f, bMonsterTruck ? 1.17f : 0.0f);',
    "VehicleInstance.cpp profile verification log")

files["Source/Runtime/Components/VehicleMovementComponent.h"] = replace_once(
    files["Source/Runtime/Components/VehicleMovementComponent.h"],
    "private:\n    UStaticMeshComponent* WheelVisuals[4] = {};",
    "private:\n    UPROPERTY(EditAnywhere)\n    bool bMonsterTruckSetup = false;\n\n    UStaticMeshComponent* WheelVisuals[4] = {};",
    "VehicleMovementComponent.h profile property")

files["Source/Runtime/Components/VehicleMovementComponent.cpp"] = replace_once(
    files["Source/Runtime/Components/VehicleMovementComponent.cpp"],
    "VehicleInstance.InitVehicle(PhysicsScene, VehicleTransform, bNotifyRigidBodyCollision)",
    "VehicleInstance.InitVehicle(PhysicsScene, VehicleTransform, bNotifyRigidBodyCollision, bMonsterTruckSetup)",
    "VehicleMovementComponent.cpp InitVehicle call")

# All exact matches are validated before touching the working tree.
for relative, updated in files.items():
    target=ROOT/relative
    if not target.exists():
        raise FileNotFoundError(f"Missing source file: {target}")

if BACKUP.exists():
    raise RuntimeError(f"Backup folder already exists: {BACKUP}. Move it aside before applying again.")
BACKUP.mkdir(parents=True)
for relative, updated in files.items():
    target=ROOT/relative
    backup=BACKUP/relative
    backup.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(target,backup)
for relative, updated in files.items():
    (ROOT/relative).write_text(updated,encoding="utf-8")
print("Monster truck 4W profile applied. Original C++ files are backed up in .monstertruck_backup/")
