#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Matrix.h"
#include "Math/Frustum.h"
#include "Math/Rect.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Containers/Array.h"
#include "Render/ShowFlags.h"
#include "Render/SceneRenderOptions.h"
#include "Engine/PostProcessSettings.h"
#include <algorithm>

// Immutable description of one camera view for the duration of a render.
struct FSceneView
{
	int32 ViewIndex = 0;
	FVector ViewLocation{};
	FRotator ViewRotation{};
	FVector ViewForward{};
	FMatrix ViewMatrix = FMatrix::Identity;
	FMatrix ProjectionMatrix = FMatrix::Identity;
	FMatrix ViewProjectionMatrix = FMatrix::Identity;
	FFrustumPlanes Frustum{};
	FFrustumCorners FrustumCorners;
	FRect ViewRect{};
	float FieldOfView = 60.0f;
	float AspectRatio = 16.0f / 9.0f;
	float NearClip = 0.1f;
	float FarClip = 10000.0f;
	float OrthoWidth = 10.0f;
	bool bIsPerspective = true;
	bool bIsWireframe = false;
	bool bGameView = false;

	FLinearColor OverlayColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // rgb = 페이드 색, a = FadeAmount
	float LetterboxAmount = 0.0f;                        // 0~1 진행도
	float LetterboxAspect = 2.39f;                       // 목표 화면비
	float VignetteIntensity = 0.0f;

	FPostProcessSettings FinalPostProcessSettings;

	// 언리얼 FSceneView::OverridePostProcessSettings와 같은 구조.
	// Src의 bOverride_*가 true인 항목만 Weight만큼 선형 보간해 덮어쓴다.
	// 정수(BladeCount)는 보간하지 않고 Weight >= 0.5일 때 대입한다.
	void OverridePostProcessSettings(const FPostProcessSettings& Src, float Weight)
	{
		const float W = std::clamp(Weight, 0.0f, 1.0f);
		if (W <= 0.0f)
		{
			return;
		}

		auto LerpFloat = [W](float& Dst, float Target)
		{
			Dst = Dst + (Target - Dst) * W;
		};

		if (Src.bOverride_DepthOfFieldFstop)           LerpFloat(FinalPostProcessSettings.DepthOfFieldFstop, Src.DepthOfFieldFstop);
		if (Src.bOverride_DepthOfFieldMinFstop)        LerpFloat(FinalPostProcessSettings.DepthOfFieldMinFstop, Src.DepthOfFieldMinFstop);
		if (Src.bOverride_DepthOfFieldBladeCount && W >= 0.5f)
		{
			FinalPostProcessSettings.DepthOfFieldBladeCount = Src.DepthOfFieldBladeCount;
		}
		if (Src.bOverride_DepthOfFieldSensorWidth)     LerpFloat(FinalPostProcessSettings.DepthOfFieldSensorWidth, Src.DepthOfFieldSensorWidth);
		if (Src.bOverride_DepthOfFieldFocalDistance)   LerpFloat(FinalPostProcessSettings.DepthOfFieldFocalDistance, Src.DepthOfFieldFocalDistance);
		if (Src.bOverride_DepthOfFieldDepthBlurAmount) LerpFloat(FinalPostProcessSettings.DepthOfFieldDepthBlurAmount, Src.DepthOfFieldDepthBlurAmount);
		if (Src.bOverride_DepthOfFieldDepthBlurRadius) LerpFloat(FinalPostProcessSettings.DepthOfFieldDepthBlurRadius, Src.DepthOfFieldDepthBlurRadius);
	}

	// 언리얼 FSceneView::EndFinalPostprocessSettings 자리.
	// 블렌딩이 끝난 최종 후처리 값의 유효 범위를 정리한다.
	void EndFinalPostprocessSettings()
	{
		FinalPostProcessSettings.DepthOfFieldMinFstop = std::clamp(FinalPostProcessSettings.DepthOfFieldMinFstop, 0.7f, 32.0f);
		FinalPostProcessSettings.DepthOfFieldFstop = std::max(FinalPostProcessSettings.DepthOfFieldFstop, FinalPostProcessSettings.DepthOfFieldMinFstop);
		FinalPostProcessSettings.DepthOfFieldBladeCount = std::clamp(FinalPostProcessSettings.DepthOfFieldBladeCount, 4, 16);
		FinalPostProcessSettings.DepthOfFieldSensorWidth = std::max(FinalPostProcessSettings.DepthOfFieldSensorWidth, 1.0f);
		FinalPostProcessSettings.DepthOfFieldFocalDistance = std::max(FinalPostProcessSettings.DepthOfFieldFocalDistance, 0.0f);
		FinalPostProcessSettings.DepthOfFieldDepthBlurAmount = std::max(FinalPostProcessSettings.DepthOfFieldDepthBlurAmount, 0.0001f);
		FinalPostProcessSettings.DepthOfFieldDepthBlurRadius = std::max(FinalPostProcessSettings.DepthOfFieldDepthBlurRadius, 0.0f);
	}

	bool NeedsCameraOverlay(bool bVignette, bool bOverlay) const
	{
		return (bOverlay && (OverlayColor.A > 0.0f || LetterboxAmount > 0.0f))
			|| (bVignette && VignetteIntensity > 0.0f);
	}

	FViewContext ToViewContext() const
	{
		FViewContext Context{};
		Context.ViewIndex = ViewIndex;
		Context.Width = static_cast<uint32>(ViewRect.Width);
		Context.Height = static_cast<uint32>(ViewRect.Height);
		Context.CameraPosition = ViewLocation;
		Context.CameraForward = ViewForward;
		Context.ViewProjection = ViewProjectionMatrix;
		Context.NearZ = NearClip;
		Context.bOrthographic = !bIsPerspective;
		Context.bGameView = bGameView;
		const float ScaleX = ProjectionMatrix.M[1][0];
		const float ScaleY = ProjectionMatrix.M[2][1];
		Context.ProjectionScaleSquared = std::max(ScaleX * ScaleX, ScaleY * ScaleY);
		Context.Prepare();
		return Context;
	}

	FViewContext ToLODViewContext() const
	{
		return ToViewContext();
	}
};

struct FCameraOverlayDebug
{
	bool  bOverride = false;
	float FadeColor[3] = { 0.0f, 0.0f, 0.0f };
	float FadeAmount = 0.0f;
	float LetterboxAmount = 0.0f;
	float LetterboxAspect = 2.39f;
	float VignetteIntensity = 0.0f;
};
inline FCameraOverlayDebug GCameraOverlayDebug;

// DoF 디버그 시각화 모드 (셰이더 DiaphragmDOFVisualize.hlsl의 VisualizeMode와 값이 같아야 한다)
enum class EDOFVisualizeMode : int32
{
	Off = 0,
	Layers = 1,   // 씬 위에 근경/DoF 범위/초점면/원경을 색으로 겹쳐 표시
	CoCOnly = 2,  // 씬 없이 CoC 크기만 표시 (초록 = 근경, 검정 = 초점, 빨강 = 원경)
};

// 마지막으로 DoF를 그린 뷰의 렌즈 계산 결과 (Settings 패널 표시용)
struct FDOFDebugStats
{
	bool  bValid = false;
	float ViewWidthPx = 0.0f;
	float FieldOfViewDeg = 0.0f;   // 세로 FOV
	float FocalLengthMm = 0.0f;
	float Fstop = 0.0f;
	float ApertureMm = 0.0f;
	float FocalDistanceM = 0.0f;
	float CoCInfinityPx = 0.0f;    // 무한대 거리의 CoC 반지름 (px)
	float MaxCoCPx = 0.0f;
	float DoFNearM = 0.0f;         // 허용 CoC 이하로 선명한 범위의 앞쪽 끝
	float DoFFarM = 0.0f;          // 뒤쪽 끝 (음수면 무한대)
	float BladeRoundness = 0.0f;
	int32 BladeCount = 0;
};

struct FDOFDebug
{
	bool  bOverride = false;
	EDOFVisualizeMode VisualizeMode = EDOFVisualizeMode::Off;
	bool  bDistanceGrid = true;       // Layers 모드에서 1m / 5m 등거리선 표시
	float InFocusCoCPx = 1.0f;        // 이 CoC 반지름(px) 이하면 "선명함(DoF 범위)"으로 본다
	// LDR 하이라이트 보정: 휘도가 Threshold를 넘는 샘플에 1 + Boost × (휘도 - Threshold) 가중치를 준다.
	// HDR이 없어 작은 광원의 빛이 넓은 보케 원판에 평균되면 어두워지므로, 밝은 샘플이 평균을 지배하게 해 보상한다.
	float HighlightThreshold = 0.7f;
	float HighlightBoost = 60.0f;
	FDOFDebugStats Stats;
	float FocalDistance = 5.0f;
	float Fstop = 1.6f;
	float MinFstop = 1.2f;
	int32 BladeCount = 5;
	float SensorWidth = 70.0f;
	float DepthBlurAmount = 1.0f;
	float DepthBlurRadius = 0.0f;
};
inline FDOFDebug GDOFDebug;

class UWorld;
struct FSceneViewTargets;

// 한 번에 그리는 뷰들의 묶음. 어디에, 무엇을, 어떤 조건으로 그릴지를 담아 FSceneRenderer에 넘긴다.
// 뷰포트 클라이언트가 Draw에서 만들고 그리기가 끝나면 버린다.
// UE의 FSceneViewFamily와 같은 자리다. RenderTarget, EngineShowFlags, ViewMode, Views는 UE에 있는 이름이다.
struct FSceneViewFamily
{
	// 그릴 대상(색상, 깊이 텍스처). FViewport가 갖고 있는 것을 가리킨다.
	FSceneViewTargets* RenderTarget = nullptr;
	// 그릴 월드. UE는 FSceneInterface* Scene을 갖는다. 이 엔진은 렌더러가 월드에서 씬을 얻으므로 월드를 담는다.
	UWorld* World = nullptr;
	// 무엇을 보여 줄지
	FEngineShowFlags EngineShowFlags;
	// 어떤 방식으로 보여 줄지
	EViewModeIndex ViewMode = VMI_Lit;
	// 품질과 렌더링 방식 설정. UE에는 없는 이 엔진 고유의 멤버다. UE는 이런 값을 콘솔 변수로 둔다.
	FSceneRenderOptions RenderOptions;
	// 이 묶음에 속한 뷰. 지금은 뷰포트마다 하나다. 뷰는 이 묶음보다 오래 살아 있어야 한다.
	TArray<const FSceneView*> Views;
};
