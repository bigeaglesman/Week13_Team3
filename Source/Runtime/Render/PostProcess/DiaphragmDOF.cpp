#include "EnginePCH.h"
#include "Render/PostProcess/DiaphragmDOF.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/SceneRenderOptions.h"
#include "Render/Texture2D.h"
#include <algorithm>
#include <cmath>

namespace
{
	FPipelineState MakeFullscreenPipeline(FShaderProgram* Shader)
	{
		FPipelineState PSO{};
		PSO.Shader = Shader;
		PSO.Topology = EPrimitiveTopology::TriangleList;
		PSO.RasterizerState = ERasterizerState::SolidNone;
		PSO.BlendState = EBlendState::Opaque;
		PSO.DepthStencilState = EDepthStencilState::Disabled;
		return PSO;
	}

	TUniquePtr<FTexture2D> CreateRenderTexture(uint32 Width, uint32 Height, ETextureFormat Format)
	{
		FTextureDesc Desc{};
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.Format = Format;
		Desc.BindFlags = ETextureBindFlags::RenderTarget | ETextureBindFlags::ShaderResource;
		auto Tex = RenderCommand::CreateTexture2D(Desc);
		if (!Tex || !Tex->GetRawPtr() || !Tex->GetRTV() || !Tex->GetSRV())
		{
			return nullptr;
		}
		return Tex;
	}
}

void FDiaphragmDOFRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;

	VisualizeCoCShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DiaphragmDOFVisualize.hlsl");
	SetupShader        = FRenderResourceManager::GetShaderProgram("Resources/Shader/DiaphragmDOFSetup.hlsl");
	NearTileShader     = FRenderResourceManager::GetShaderProgram("Resources/Shader/DiaphragmDOFNearTile.hlsl");
	NearDilateShader   = FRenderResourceManager::GetShaderProgram("Resources/Shader/DiaphragmDOFNearDilate.hlsl");
	GatherShader       = FRenderResourceManager::GetShaderProgram("Resources/Shader/DiaphragmDOFGather.hlsl");
	PostfilterShader   = FRenderResourceManager::GetShaderProgram("Resources/Shader/DiaphragmDOFPostfilter.hlsl");
	RecombineShader    = FRenderResourceManager::GetShaderProgram("Resources/Shader/DiaphragmDOFRecombine.hlsl");

	VisualizeCoCPipeline = MakeFullscreenPipeline(VisualizeCoCShader);
	SetupPipeline        = MakeFullscreenPipeline(SetupShader);
	NearTilePipeline     = MakeFullscreenPipeline(NearTileShader);
	NearDilatePipeline   = MakeFullscreenPipeline(NearDilateShader);
	GatherPipeline       = MakeFullscreenPipeline(GatherShader);
	PostfilterPipeline   = MakeFullscreenPipeline(PostfilterShader);
	RecombinePipeline    = MakeFullscreenPipeline(RecombineShader);

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDOFData));
	assert(VisualizeCoCShader && SetupShader && NearTileShader && NearDilateShader &&
		GatherShader && PostfilterShader && RecombineShader &&
		ConstantBuffer && ConstantBuffer->GetBuffer());
}

FPostProcessSettings FDiaphragmDOFRenderer::ResolveEffectiveSettings(const FSceneView& View)
{
	FPostProcessSettings Settings = View.FinalPostProcessSettings;
	if (GDOFDebug.bOverride)
	{
		Settings.DepthOfFieldFocalDistance = std::max(GDOFDebug.FocalDistance, 0.0f);
		Settings.DepthOfFieldMinFstop = std::clamp(GDOFDebug.MinFstop, 0.7f, 32.0f);
		Settings.DepthOfFieldFstop = std::max(GDOFDebug.Fstop, Settings.DepthOfFieldMinFstop);
		Settings.DepthOfFieldBladeCount = std::clamp(GDOFDebug.BladeCount, 4, 16);
		Settings.DepthOfFieldSensorWidth = std::max(GDOFDebug.SensorWidth, 1.0f);
		Settings.DepthOfFieldDepthBlurAmount = std::max(GDOFDebug.DepthBlurAmount, 0.0001f);
		Settings.DepthOfFieldDepthBlurRadius = std::max(GDOFDebug.DepthBlurRadius, 0.0f);
	}
	return Settings;
}

bool FDiaphragmDOFRenderer::ShouldRun(const FSceneView& View)
{
	if (!View.bIsPerspective || View.bIsWireframe)
	{
		return false;
	}
	const FPostProcessSettings Settings = ResolveEffectiveSettings(View);
	return Settings.DepthOfFieldFocalDistance > 0.0f;
}

FDiaphragmDOFRenderer::FCachedTargetSet* FDiaphragmDOFRenderer::GetOrCreateTargets(FTexture2D* ColorTarget)
{
	if (!ColorTarget) return nullptr;

	const uint32 FullW = ColorTarget->GetWidth();
	const uint32 FullH = ColorTarget->GetHeight();
	const DXGI_FORMAT Format = ColorTarget->GetFormat();
	if (!FullW || !FullH) return nullptr;

	for (FCachedTargetSet& Set : CachedTargets)
	{
		if (Set.FullWidth == FullW && Set.FullHeight == FullH && Set.ColorFormat == Format)
		{
			return &Set;
		}
	}

	// 에디터 분할 뷰포트 등 최대 6개 해상도까지 캐시 유지, 초과 시 가장 오래된 항목 교체
	if (CachedTargets.Num() >= 6)
	{
		CachedTargets.RemoveAt(0);
	}

	const uint32 HalfW = std::max((FullW + 1u) / 2u, 1u);
	const uint32 HalfH = std::max((FullH + 1u) / 2u, 1u);
	const uint32 TileW = std::max((HalfW + 3u) / 4u, 1u);
	const uint32 TileH = std::max((HalfH + 3u) / 4u, 1u);

	FCachedTargetSet NewSet{};
	NewSet.FullWidth = FullW;
	NewSet.FullHeight = FullH;
	NewSet.HalfWidth = HalfW;
	NewSet.HalfHeight = HalfH;
	NewSet.TileWidth = TileW;
	NewSet.TileHeight = TileH;
	NewSet.ColorFormat = Format;

	NewSet.SceneColorCopy     = CreateRenderTexture(FullW, FullH, ColorTarget->GetTextureFormat());
	NewSet.HalfColorCoC       = CreateRenderTexture(HalfW, HalfH, ETextureFormat::R16G16B16A16_Float);
	NewSet.NearCoCTileRaw     = CreateRenderTexture(TileW, TileH, ETextureFormat::R16_Float);
	NewSet.NearCoCTileDilated = CreateRenderTexture(TileW, TileH, ETextureFormat::R16_Float);
	NewSet.HalfFar            = CreateRenderTexture(HalfW, HalfH, ETextureFormat::R16G16B16A16_Float);
	NewSet.HalfNear           = CreateRenderTexture(HalfW, HalfH, ETextureFormat::R16G16B16A16_Float);
	NewSet.HalfFarFiltered    = CreateRenderTexture(HalfW, HalfH, ETextureFormat::R16G16B16A16_Float);
	NewSet.HalfNearFiltered   = CreateRenderTexture(HalfW, HalfH, ETextureFormat::R16G16B16A16_Float);

	if (!NewSet.SceneColorCopy || !NewSet.HalfColorCoC || !NewSet.NearCoCTileRaw ||
		!NewSet.NearCoCTileDilated || !NewSet.HalfFar || !NewSet.HalfNear ||
		!NewSet.HalfFarFiltered || !NewSet.HalfNearFiltered)
	{
		return nullptr;
	}

	CachedTargets.Add(std::move(NewSet));
	return &CachedTargets.Last();
}

FDOFData FDiaphragmDOFRenderer::BuildConstants(const FSceneView& View, const FSceneRenderOptions& Options,
	uint32 FullWidth, uint32 FullHeight) const
{
	const FPostProcessSettings PP = ResolveEffectiveSettings(View);
	const uint32 HalfWidth = std::max((FullWidth + 1u) / 2u, 1u);
	const uint32 HalfHeight = std::max((FullHeight + 1u) / 2u, 1u);

	const float WidthF = static_cast<float>(std::max(FullWidth, 1u));
	const float HeightF = static_cast<float>(std::max(FullHeight, 1u));

	// 세로 FOV → 가로 FOV 변환: tan(HFOV/2) = tan(VFOV/2) * AspectRatio
	const float HalfVFOVRad = FMath::DegreesToRadians(std::clamp(View.FieldOfView, 1.0f, 179.0f)) * 0.5f;
	const float TanHalfHFOV = std::max(std::tan(HalfVFOVRad) * std::max(View.AspectRatio, 0.01f), 1e-4f);

	const float SensorWidthMm = std::max(PP.DepthOfFieldSensorWidth, 1.0f);
	const float FocalLenMm = (SensorWidthMm * 0.5f) / TanHalfHFOV;

	const float MinFstop = std::clamp(PP.DepthOfFieldMinFstop, 0.7f, 32.0f);
	const float Fstop = std::max(PP.DepthOfFieldFstop, MinFstop);
	const float ApertureMm = FocalLenMm / Fstop;

	// 초점 거리가 렌즈 초점 길이보다 가까우면 수식이 깨지므로 안전 하한 적용
	const float FocalDistMm = std::max(PP.DepthOfFieldFocalDistance * 1000.0f, FocalLenMm + 1.0f);
	const float CoCInfinityMm = (ApertureMm * FocalLenMm) / (FocalDistMm - FocalLenMm);
	const float CoCScalePx = 0.5f * (CoCInfinityMm / SensorWidthMm) * WidthF;

	// Fstop이 MinFstop에 아주 가까울 때만 원형으로 열리고, 조여질수록 다각형 보케가 뚜렷해진다.
	const float BladeRoundness = std::clamp(1.0f - (Fstop - MinFstop) / 0.4f, 0.0f, 1.0f);

	FDOFData Data{};
	Data.InvFullWidth = 1.0f / WidthF;
	Data.InvFullHeight = 1.0f / HeightF;
	Data.InvHalfWidth = 1.0f / static_cast<float>(HalfWidth);
	Data.InvHalfHeight = 1.0f / static_cast<float>(HalfHeight);

	Data.NearClip = std::max(View.NearClip, 1e-4f);
	Data.FarClip = std::max(View.FarClip, Data.NearClip + 0.1f);
	Data.FocalDistance = FocalDistMm * 0.001f;
	Data.CoCScale = CoCScalePx;

	Data.DepthBlurAmountKm = std::max(PP.DepthOfFieldDepthBlurAmount, 0.0001f);
	Data.DepthBlurRadiusPx = std::max(PP.DepthOfFieldDepthBlurRadius, 0.0f) * (WidthF / 1920.0f);
	Data.MaxCoCRadiusPx = 0.025f * WidthF;
	Data.DisplayGamma = Options.bGammaCorrection ? std::max(Options.DisplayGamma, 0.1f) : 1.0f;

	Data.BladeCount = std::clamp(PP.DepthOfFieldBladeCount, 4, 16);
	Data.BladeRoundness = BladeRoundness;
	Data.HighlightThreshold = 0.75f;
	Data.HighlightBoost = 5.0f;
	return Data;
}

bool FDiaphragmDOFRenderer::RenderVisualizeCoC(const FDOFData& Data, FTexture2D* ColorTarget, FTexture2D* DepthTarget)
{
	if (!VisualizeCoCShader || !ConstantBuffer || !ColorTarget || !DepthTarget)
	{
		return false;
	}

	RenderCommand::SetRenderTargets(ColorTarget, nullptr);
	RenderCommand::SetViewport(0, 0, ColorTarget->GetWidth(), ColorTarget->GetHeight());
	RenderCommand::BindPipelineState(VisualizeCoCPipeline);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(3);
	RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Pixel);
	return true;
}

bool FDiaphragmDOFRenderer::OnRender(const FSceneView& View, const FSceneRenderOptions& Options,
	FTexture2D* ColorTarget, FTexture2D* DepthTarget)
{
	if (!ColorTarget || !DepthTarget || !ConstantBuffer)
	{
		return false;
	}

	const uint32 FullW = ColorTarget->GetWidth();
	const uint32 FullH = ColorTarget->GetHeight();
	if (!FullW || !FullH) return false;

	const FDOFData Data = BuildConstants(View, Options, FullW, FullH);

	if (GDOFDebug.bVisualizeCoC)
	{
		return RenderVisualizeCoC(Data, ColorTarget, DepthTarget);
	}

	FCachedTargetSet* Targets = GetOrCreateTargets(ColorTarget);
	if (!Targets)
	{
		return false;
	}

	// 공통 리소스 바인딩 준비
	RenderCommand::UnbindShaderResources(0, 4, EShaderBindFlagBits::Pixel);
	RenderCommand::SetRenderTargets(0, nullptr, nullptr);

	// 0) SceneColor → SceneColorCopy (RTV/SRV 동시 바인딩 방지)
	RenderCommand::CopyTexture(Targets->SceneColorCopy.get(), ColorTarget);

	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);

	// 1) DOFSetup (Half Res): 감마 해제 + Karis 다운샘플 + 부호 있는 CoC
	{
		RenderCommand::SetRenderTargets(Targets->HalfColorCoC.get(), nullptr);
		RenderCommand::SetViewport(0, 0, Targets->HalfWidth, Targets->HalfHeight);
		RenderCommand::BindPipelineState(SetupPipeline);
		RenderCommand::BindShaderResource(0, Targets->SceneColorCopy.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::BindShaderResource(1, DepthTarget, EShaderBindFlagBits::Pixel);
		RenderCommand::Draw(3);
		RenderCommand::UnbindShaderResources(0, 2, EShaderBindFlagBits::Pixel);
	}

	// 2) DOFNearTile (1/8 Res): 타일별 전경 최대 CoC 추출
	{
		RenderCommand::SetRenderTargets(Targets->NearCoCTileRaw.get(), nullptr);
		RenderCommand::SetViewport(0, 0, Targets->TileWidth, Targets->TileHeight);
		RenderCommand::BindPipelineState(NearTilePipeline);
		RenderCommand::BindShaderResource(0, Targets->HalfColorCoC.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::Draw(3);
		RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Pixel);
	}

	// 3) DOFNearDilate (1/8 Res): 전경 타일 5x5 확장 (전경 실루엣 바깥 번짐용)
	{
		RenderCommand::SetRenderTargets(Targets->NearCoCTileDilated.get(), nullptr);
		RenderCommand::SetViewport(0, 0, Targets->TileWidth, Targets->TileHeight);
		RenderCommand::BindPipelineState(NearDilatePipeline);
		RenderCommand::BindShaderResource(0, Targets->NearCoCTileRaw.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::Draw(3);
		RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Pixel);
	}

	// 4) DOFGather (Half Res, MRT 2장): 다각형 조리개 커널로 Far / Near 분리 누적
	{
		FTexture2D* GatherRTVs[2] = { Targets->HalfFar.get(), Targets->HalfNear.get() };
		RenderCommand::SetRenderTargets(2, GatherRTVs, nullptr);
		RenderCommand::SetViewport(0, 0, Targets->HalfWidth, Targets->HalfHeight);
		RenderCommand::BindPipelineState(GatherPipeline);
		RenderCommand::BindShaderResource(0, Targets->HalfColorCoC.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::BindShaderResource(1, Targets->NearCoCTileDilated.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::Draw(3);
		RenderCommand::UnbindShaderResources(0, 2, EShaderBindFlagBits::Pixel);
	}

	// 5) DOFPostfilter (Half Res, MRT 2장): 3x3 텐트 필터로 샘플 구멍·노이즈 정리
	{
		FTexture2D* FilterRTVs[2] = { Targets->HalfFarFiltered.get(), Targets->HalfNearFiltered.get() };
		RenderCommand::SetRenderTargets(2, FilterRTVs, nullptr);
		RenderCommand::SetViewport(0, 0, Targets->HalfWidth, Targets->HalfHeight);
		RenderCommand::BindPipelineState(PostfilterPipeline);
		RenderCommand::BindShaderResource(0, Targets->HalfFar.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::BindShaderResource(1, Targets->HalfNear.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::Draw(3);
		RenderCommand::UnbindShaderResources(0, 2, EShaderBindFlagBits::Pixel);
	}

	// 6) DOFRecombine (Full Res → Targets.SceneColor): 원본 + Far + Near 최종 합성 및 감마 재인코딩
	{
		RenderCommand::SetRenderTargets(ColorTarget, nullptr);
		RenderCommand::SetViewport(0, 0, FullW, FullH);
		RenderCommand::BindPipelineState(RecombinePipeline);
		RenderCommand::BindShaderResource(0, Targets->SceneColorCopy.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::BindShaderResource(1, DepthTarget, EShaderBindFlagBits::Pixel);
		RenderCommand::BindShaderResource(2, Targets->HalfFarFiltered.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::BindShaderResource(3, Targets->HalfNearFiltered.get(), EShaderBindFlagBits::Pixel);
		RenderCommand::Draw(3);
		RenderCommand::UnbindShaderResources(0, 4, EShaderBindFlagBits::Pixel);
	}

	return true;
}
