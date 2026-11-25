#pragma once

#include "Render/Renderer.h"
#include "Render/PipelineState.h"
#include "Engine/PostProcessSettings.h"
#include "Containers/Array.h"

struct FSceneView;
struct FSceneRenderOptions;
class FTexture2D;

// DoF 전 패스(CoC 시각화, Setup, NearTile, NearDilate, Gather, Postfilter, Recombine)가 공유하는 상수 버퍼.
struct alignas(16) FDOFData
{
	float InvFullWidth;
	float InvFullHeight;
	float InvHalfWidth;
	float InvHalfHeight;

	float NearClip;           // m
	float FarClip;            // m
	float FocalDistance;      // m
	float CoCScale;           // 무한대 거리에서의 풀 해상도 CoC 반지름 (px)

	float DepthBlurAmountKm;  // 원거리 흐림 50% 도달 거리 (km)
	float DepthBlurRadiusPx;  // 현재 뷰 폭에 맞게 스케일된 원거리 흐림 최대 반경 (px)
	float MaxCoCRadiusPx;     // 최대 허용 CoC 반지름 (화면 폭의 2.5%, px)
	float DisplayGamma;       // 선형화/재인코딩 감마 (감마 보정 꺼짐 시 1.0f)

	int32 BladeCount;         // 조리개 날 수 (4~16)
	float BladeRoundness;     // 0 = 완전 다각형, 1 = 완전 원형 (Fstop이 MinFstop에 가까울수록 1)
	float HighlightThreshold; // LDR 하이라이트 부스트 임계값
	float HighlightBoost;     // LDR 하이라이트 부스트 강도
};
static_assert(sizeof(FDOFData) == 64);

// 언리얼의 DiaphragmDOF 이름을 따른 피사계 심도(Depth of Field) 후처리 렌더러.
class FDiaphragmDOFRenderer
{
public:
	void Init(FRenderer* InRenderer);

	// 현재 뷰와 디버그 설정을 합쳐 최종 사용할 FPostProcessSettings를 반환한다.
	static FPostProcessSettings ResolveEffectiveSettings(const FSceneView& View);

	// 패스를 등록할지 여부 (FocalDistance > 0 && 원근 카메라 등)
	static bool ShouldRun(const FSceneView& View);

	// CoC 시각화 또는 실제 다각형 보케 Gather 파이프라인 실행 (결과를 ColorTarget에 다시 쓴다)
	bool OnRender(const FSceneView& View, const FSceneRenderOptions& Options,
		FTexture2D* ColorTarget, FTexture2D* DepthTarget);

private:
	struct FCachedTargetSet
	{
		uint32 FullWidth = 0;
		uint32 FullHeight = 0;
		uint32 HalfWidth = 0;
		uint32 HalfHeight = 0;
		uint32 TileWidth = 0;
		uint32 TileHeight = 0;
		DXGI_FORMAT ColorFormat = DXGI_FORMAT_UNKNOWN;

		TUniquePtr<FTexture2D> SceneColorCopy;
		TUniquePtr<FTexture2D> HalfColorCoC;
		TUniquePtr<FTexture2D> NearCoCTileRaw;
		TUniquePtr<FTexture2D> NearCoCTileDilated;
		TUniquePtr<FTexture2D> HalfFar;
		TUniquePtr<FTexture2D> HalfNear;
		TUniquePtr<FTexture2D> HalfFarFiltered;
		TUniquePtr<FTexture2D> HalfNearFiltered;
	};

	FCachedTargetSet* GetOrCreateTargets(FTexture2D* ColorTarget);
	bool RenderVisualizeCoC(const FDOFData& Data, FTexture2D* ColorTarget, FTexture2D* DepthTarget);

	FDOFData BuildConstants(const FSceneView& View, const FSceneRenderOptions& Options,
		uint32 FullWidth, uint32 FullHeight) const;

	FRenderer* Renderer = nullptr;

	FShaderProgram* VisualizeCoCShader = nullptr;
	FShaderProgram* SetupShader = nullptr;
	FShaderProgram* NearTileShader = nullptr;
	FShaderProgram* NearDilateShader = nullptr;
	FShaderProgram* GatherShader = nullptr;
	FShaderProgram* PostfilterShader = nullptr;
	FShaderProgram* RecombineShader = nullptr;

	FPipelineState VisualizeCoCPipeline;
	FPipelineState SetupPipeline;
	FPipelineState NearTilePipeline;
	FPipelineState NearDilatePipeline;
	FPipelineState GatherPipeline;
	FPipelineState PostfilterPipeline;
	FPipelineState RecombinePipeline;

	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TArray<FCachedTargetSet> CachedTargets;
};
