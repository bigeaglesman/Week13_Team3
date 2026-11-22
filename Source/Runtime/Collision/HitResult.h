#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"

class AActor;
class UPrimitiveComponent;

struct FHitResult
{
    int32 FaceIndex = -1;

    float Time = 1.0f;
    float Distance = FLT_MAX;

    FVector Location = FVector();
    FVector ImpactPoint = FVector();
    FVector Normal = FVector();
    FVector ImpactNormal = FVector();

    FVector TraceStart = FVector();
    FVector TraceEnd = FVector();

    float PenetrationDepth = 0.0f;

    bool bBlockingHit = false;
    bool bStartPenetrating = false;

    UPrimitiveComponent* HitComponent = nullptr;

    FHitResult() = default;

    explicit FHitResult(float InTime)
        : Time(InTime)
    {
    }

    void Init()
    {
        FaceIndex = -1;
        Time = 1.0f;
        Distance = FLT_MAX;
        Location = FVector();
        ImpactPoint = FVector();
        Normal = FVector();
        ImpactNormal = FVector();
        TraceStart = FVector();
        TraceEnd = FVector();
        PenetrationDepth = 0.0f;
        bBlockingHit = false;
        bStartPenetrating = false;
        HitComponent = nullptr;
    }

    void Init(FVector Start, FVector End)
    {
        Init();
        TraceStart = Start;
        TraceEnd = End;
    }
};

struct FOverlapResult
{
	AActor* Actor = nullptr;
	UPrimitiveComponent* Component = nullptr;
	int32 ItemIndex = -1;
	bool bBlockingHit = false;
};

using FSweepResult = FHitResult;