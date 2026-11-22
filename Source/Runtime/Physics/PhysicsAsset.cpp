
#include "EnginePCH.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicalMaterial.h"
#include "Render/SkeletalMesh.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/StructuredArchiveSlots.h"

#include <cmath>

namespace
{
    const char* GetShapeTypeName(EPhysicsShapeType Type)
    {
        switch (Type)
        {
        case EPhysicsShapeType::Box: return "Box";
        case EPhysicsShapeType::Sphere: return "Sphere";
        case EPhysicsShapeType::Capsule: return "Capsule";
        default: return "";
        }
    }

    bool ParseShapeType(const FString& Name, EPhysicsShapeType& OutType)
    {
        if (Name == "Box") OutType = EPhysicsShapeType::Box;
        else if (Name == "Sphere") OutType = EPhysicsShapeType::Sphere;
        else if (Name == "Capsule") OutType = EPhysicsShapeType::Capsule;
        else return false;

        return true;
    }

    const char* GetMotionName(EPhysicsConstraintMotion Motion)
    {
        switch (Motion)
        {
        case EPhysicsConstraintMotion::Locked: return "Locked";
        case EPhysicsConstraintMotion::Limited: return "Limited";
        case EPhysicsConstraintMotion::Free: return "Free";
        default: return "";
        }
    }

    bool ParseMotion(const FString& Name, EPhysicsConstraintMotion& OutMotion)
    {
        if (Name == "Locked") OutMotion = EPhysicsConstraintMotion::Locked;
        else if (Name == "Limited") OutMotion = EPhysicsConstraintMotion::Limited;
        else if (Name == "Free") OutMotion = EPhysicsConstraintMotion::Free;
        else return false;

        return true;
    }

    void SerializeMotion(FStructuredArchive::FRecord Record, const char* FieldName, EPhysicsConstraintMotion& Motion)
    {
        FArchive& Ar = Record.GetUnderlyingArchive();

        FString Name = Ar.IsSaving() ? GetMotionName(Motion) : FString();
        Record << SA_VALUE(FieldName, Name);

        if (Ar.IsLoading() && !ParseMotion(Name, Motion))
            Ar.SetError();
    }

    void SerializeShape(FStructuredArchive::FRecord Record, FPhysicsShapeSetup& Shape, UObject* Outer)
    {
        FArchive& Ar = Record.GetUnderlyingArchive();

        FString TypeName = Ar.IsSaving() ? GetShapeTypeName(Shape.ShapeType) : FString();

        Record << SA_VALUE("Type", TypeName);
        Record << SA_VALUE("LocalPose", Shape.LocalPose);
        Record << SA_VALUE("Dimensions", Shape.Dimensions);
        Record << SA_VALUE("StaticFriction", Shape.StaticFriction);
        Record << SA_VALUE("DynamicFriction", Shape.DynamicFriction);
        Record << SA_VALUE("Restitution", Shape.Restitution);

        if (Ar.IsLoading() && !ParseShapeType(TypeName, Shape.ShapeType))
            Ar.SetError();

        const bool bHasPhysicalMaterial = Ar.IsSaving() && Shape.PhysicalMaterial != nullptr;

        if (TOptional<FStructuredArchive::FSlot> MaterialSlot = Record.TryEnterField("PhysicalMaterial", bHasPhysicalMaterial))
        {
            if (Ar.IsLoading())
            {
                Shape.PhysicalMaterial = NewObject<UPhysicalMaterial>(Outer);
            }

            if (!Shape.PhysicalMaterial)
            {
                Ar.SetError();
                return;
            }

            Shape.PhysicalMaterial->Serialize(MaterialSlot.GetValue().EnterRecord());
        }
        else if (Ar.IsLoading())
        {
            Shape.PhysicalMaterial = NewObject<UPhysicalMaterial>(Outer);

            if (!Shape.PhysicalMaterial)
            {
                Ar.SetError();
                return;
            }

            Shape.PhysicalMaterial->StaticFriction = Shape.StaticFriction;
            Shape.PhysicalMaterial->DynamicFriction = Shape.DynamicFriction;
            Shape.PhysicalMaterial->Restitution = Shape.Restitution;
        }
    }

    void SerializeBodySetup(FStructuredArchive::FRecord Record, FPhysicsBodySetup& Body, UObject* Outer)
    {
        FArchive& Ar = Record.GetUnderlyingArchive();

        Record << SA_VALUE("BoneName", Body.BoneName);
        Record << SA_VALUE("Mass", Body.Mass);
        Record << SA_VALUE("LinearDamping", Body.LinearDamping);
        Record << SA_VALUE("AngularDamping", Body.AngularDamping);
        Record << SA_VALUE("EnableGravity", Body.bEnableGravity);
        Record << SA_VALUE("StartAwake", Body.bStartAwake);
        Record << SA_VALUE("Kinematic", Body.bKinematic);

        int32 ShapeCount = Ar.IsSaving() ? Body.Shapes.Num() : 0;
        auto Shapes = Record.EnterArray("Shapes", ShapeCount);

        if (Ar.IsLoading())
            Body.Shapes.SetNum(ShapeCount);

        for (int32 Index = 0; Index < ShapeCount; ++Index)
            SerializeShape(Shapes.EnterElement().EnterRecord(), Body.Shapes[Index], Outer);
    }

    void SerializeConstraintSetup(FStructuredArchive::FRecord Record, FPhysicsConstraintSetup& Constraint)
    {
        FArchive& Ar = Record.GetUnderlyingArchive();

        Record << SA_VALUE("BoneA", Constraint.BoneNameA);
        Record << SA_VALUE("BoneB", Constraint.BoneNameB);
        Record << SA_VALUE("FrameA", Constraint.FrameA);
        Record << SA_VALUE("FrameB", Constraint.FrameB);

        SerializeMotion(Record, "LinearX", Constraint.LinearX);
        SerializeMotion(Record, "LinearY", Constraint.LinearY);
        SerializeMotion(Record, "LinearZ", Constraint.LinearZ);
        Record << SA_VALUE("LinearLimit", Constraint.LinearLimit);
        SerializeMotion(Record, "Swing1", Constraint.Swing1);
        SerializeMotion(Record, "Swing2", Constraint.Swing2);
        SerializeMotion(Record, "Twist", Constraint.Twist);

        Record << SA_VALUE("Swing1Limit", Constraint.Swing1Limit);
        Record << SA_VALUE("Swing2Limit", Constraint.Swing2Limit);
        Record << SA_VALUE("TwistLimit", Constraint.TwistLimit);
    }
}


void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);

    FArchive& Ar = Record.GetUnderlyingArchive();

    int32 Version = Ar.IsSaving() ? 1 : 0;
    Record << SA_VALUE("Version", Version);

    if (Version != 1)
    {
        Ar.SetError();
        return;
    }

    Record << SA_VALUE("SkeletalMeshPath", SkeletalMeshPath);

    int32 BodyCount = Ar.IsSaving() ? BodySetups.Num() : 0;
    auto Bodies = Record.EnterArray("Bodies", BodyCount);

    if (Ar.IsLoading())
        BodySetups.SetNum(BodyCount);

    for (int32 Index = 0; Index < BodyCount; ++Index)
        SerializeBodySetup(Bodies.EnterElement().EnterRecord(), BodySetups[Index], this);

    int32 ConstraintCount = Ar.IsSaving() ? ConstraintSetups.Num() : 0;
    auto Constraints = Record.EnterArray("Constraints", ConstraintCount);

    if (Ar.IsLoading())
        ConstraintSetups.SetNum(ConstraintCount);

    for (int32 Index = 0; Index < ConstraintCount; ++Index)
        SerializeConstraintSetup(Constraints.EnterElement().EnterRecord(), ConstraintSetups[Index]);
}

bool UPhysicsAsset::ValidateAgainstSkeletalMesh(const USkeletalMesh* SkeletalMesh, FString& OutError) const
{
    OutError.clear();

    if (!SkeletalMesh)
    {
        OutError = "PhysicsAsset: SkeletalMesh is null";
        return false;
    }

    auto HasBodySetup = [this](const FString& BoneName)
        {
            for (const FPhysicsBodySetup& Body : BodySetups)
                if (Body.BoneName == BoneName)
                    return true;

            return false;
        };

    for (int32 Index = 0; Index < BodySetups.Num(); ++Index)
    {
        const FPhysicsBodySetup& Body = BodySetups[Index];

        if (Body.BoneName.empty() || SkeletalMesh->FindBoneIndex(Body.BoneName) < 0)
        {
            OutError = "PhysicsAsset: Body references an invalid bone";
            return false;
        }

        for (int32 Previous = 0; Previous < Index; ++Previous)
        {
            if (BodySetups[Previous].BoneName == Body.BoneName)
            {
                OutError = "PhysicsAsset: Duplicate body for bone";
                return false;
            }
        }

        if (!std::isfinite(Body.Mass) || Body.Mass <= 0.0f ||
            !std::isfinite(Body.LinearDamping) || Body.LinearDamping < 0.0f ||
            !std::isfinite(Body.AngularDamping) || Body.AngularDamping < 0.0f)
        {
            OutError = "PhysicsAsset: Invalid body physical properties";
            return false;
        }

        if (Body.Shapes.IsEmpty())
        {
            OutError = "PhysicsAsset: Body has no collision shapes";
            return false;
        }

        for (const FPhysicsShapeSetup& Shape : Body.Shapes)
        {
            if (Shape.PhysicalMaterial)
            {
                if (!Shape.PhysicalMaterial->IsValid())
                {
                    OutError = "PhysicsAsset: Invalid physical material";
                    return false;
                }
            }
            else if (!std::isfinite(Shape.StaticFriction) || Shape.StaticFriction < 0.0f ||
                !std::isfinite(Shape.DynamicFriction) || Shape.DynamicFriction < 0.0f ||
                !std::isfinite(Shape.Restitution) || Shape.Restitution < 0.0f || Shape.Restitution > 1.0f)
            {
                OutError = "PhysicsAsset: Invalid shape material";
                return false;
            }

            const FVector& D = Shape.Dimensions;

            if (Shape.ShapeType == EPhysicsShapeType::Box &&
                (D.X <= 0.0f || D.Y <= 0.0f || D.Z <= 0.0f))
            {
                OutError = "PhysicsAsset: Invalid box dimensions";
                return false;
            }

            if (Shape.ShapeType == EPhysicsShapeType::Sphere &&
                (D.X <= 0.0f))
            {
                OutError = "PhysicsAsset: Invalid sphere radius";
                return false;
            }

            if (Shape.ShapeType == EPhysicsShapeType::Capsule &&
                (D.X <= 0.0f || D.Z < 0.0f))
            {
                OutError = "PhysicsAsset: Invalid capsule dimensions";
                return false;
            }
        }
    }

    for (const FPhysicsConstraintSetup& Constraint : ConstraintSetups)
    {
        if (Constraint.BoneNameA == Constraint.BoneNameB ||
            SkeletalMesh->FindBoneIndex(Constraint.BoneNameA) < 0 ||
            SkeletalMesh->FindBoneIndex(Constraint.BoneNameB) < 0 ||
            !HasBodySetup(Constraint.BoneNameA) ||
            !HasBodySetup(Constraint.BoneNameB))
        {
            OutError = "PhysicsAsset: Constraint references an invalid body";
            return false;
        }

        const bool bHasLimitedLinear = Constraint.LinearX == EPhysicsConstraintMotion::Limited ||
            Constraint.LinearY == EPhysicsConstraintMotion::Limited ||
            Constraint.LinearZ == EPhysicsConstraintMotion::Limited;

        if (!std::isfinite(Constraint.LinearLimit) || Constraint.LinearLimit < 0.0f ||
            (bHasLimitedLinear && Constraint.LinearLimit <= 0.0f) ||
            !std::isfinite(Constraint.Swing1Limit) || Constraint.Swing1Limit < 0.0f ||
            !std::isfinite(Constraint.Swing2Limit) || Constraint.Swing2Limit < 0.0f ||
            !std::isfinite(Constraint.TwistLimit) || Constraint.TwistLimit < 0.0f ||
            (Constraint.Swing1 == EPhysicsConstraintMotion::Limited && (Constraint.Swing1Limit <= 0.0f || Constraint.Swing1Limit >= 180.0f)) ||
            (Constraint.Swing2 == EPhysicsConstraintMotion::Limited && (Constraint.Swing2Limit <= 0.0f || Constraint.Swing2Limit >= 180.0f)) ||
            (Constraint.Twist == EPhysicsConstraintMotion::Limited && (Constraint.TwistLimit <= 0.0f || Constraint.TwistLimit > 180.0f)))
        {
            OutError = "PhysicsAsset: Invalid constraint limits";
            return false;
        }
    }

    return true;
}
