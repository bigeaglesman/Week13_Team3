cbuffer DOFConstants : register(b0)
{
    float InvFullWidth;
    float InvFullHeight;
    float InvHalfWidth;
    float InvHalfHeight;

    float NearClip;
    float FarClip;
    float FocalDistance;
    float CoCScale;

    float DepthBlurAmountKm;
    float DepthBlurRadiusPx;
    float MaxCoCRadiusPx;
    float DisplayGamma;

    int BladeCount;
    float BladeRoundness;
    float HighlightThreshold;
    float HighlightBoost;
};

Texture2D<float4> HalfColorCoCTexture : register(t0);
Texture2D<float>  NearCoCTileDilated  : register(t1);
SamplerState LinearClampSampler       : register(s0);

static const float PI = 3.14159265359;
static const float TWO_PI = 6.28318530718;

struct VSOutput
{
    float4 Position : SV_Position;
};

struct PSOutput
{
    // 두 레이어 모두 premultiplied(rgb = 색 × a)로 저장한다.
    // Postfilter/Recombine의 bilinear 필터가 a = 0인 텍셀의 색(초점 맞은 물체 색)을 섞어 테두리를 만들지 않게 한다.
    float4 FarLayer  : SV_Target0; // rgb = 배경 선형 색 × a, a = 커버리지
    float4 NearLayer : SV_Target1; // rgb = 전경 선형 색 × a, a = 전경 커버리지(0~1)
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    VSOutput Output;
    Output.Position = float4(UV * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return Output;
}

float GetLuma(float3 Color)
{
    return dot(Color, float3(0.299, 0.587, 0.114));
}

// Interleaved Gradient Noise (Jimenez 2014). 픽셀마다 0~1 값을 주어 샘플 패턴을 흔든다.
// 고정 링 패턴이 큰 보케에서 점선·고리로 보이는 것을 노이즈로 바꾸고, Postfilter가 그 노이즈를 정리한다.
float InterleavedGradientNoise(float2 Pixel)
{
    return frac(52.9829189 * frac(dot(Pixel, float2(0.06711056, 0.00583715))));
}

// 조리개 날 수(N)에 따라 원판 반지름 r을 N각형 다각형으로 접는다.
float2 ShapeDiaphragm(float Radius, float Angle)
{
    float N = (float)max(BladeCount, 4);
    float Sector = TWO_PI / N;
    float HalfSector = PI / N;
    float LocalAngle = fmod(Angle + TWO_PI, Sector) - HalfSector;
    float PolyScale = cos(HalfSector) / max(cos(LocalAngle), 0.1);
    // BladeRoundness가 0이면 또렷한 N각형, 1이면 원형
    float ShapedRadius = Radius * lerp(PolyScale, 1.0, BladeRoundness);
    return float2(cos(Angle), sin(Angle)) * ShapedRadius;
}

PSOutput mainPS(VSOutput Input)
{
    PSOutput Out;
    float2 HalfTexelSize = float2(InvHalfWidth, InvHalfHeight);
    float2 CenterUV = Input.Position.xy * HalfTexelSize;

    float4 CenterData = HalfColorCoCTexture.SampleLevel(LinearClampSampler, CenterUV, 0);
    float CenterCoC = CenterData.a; // 하프 픽셀 단위 부호 있는 CoC
    float CenterFarCoC = max(CenterCoC, 0.0);
    float DilatedNearCoC = NearCoCTileDilated.SampleLevel(LinearClampSampler, CenterUV, 0);

    // 중심 + 4개 동심원 링 (1 + 8 + 16 + 24 + 32 = 81 샘플)
    const int RingCount = 4;
    const int SamplesPerRing[4] = { 8, 16, 24, 32 };

    // 픽셀마다 각도와 링 반지름을 흔든다. 다각형 모양은 절대 각도(ShapeDiaphragm)로 정해지므로
    // 각도를 돌려도 보케 모양은 그대로고 샘플 위치만 바뀐다.
    const float AngleJitter = InterleavedGradientNoise(Input.Position.xy);
    const float RadiusJitter = InterleavedGradientNoise(Input.Position.xy + float2(17.0, 59.0));

    float3 FarAccum = float3(0.0, 0.0, 0.0);
    float  FarWeightSum = 0.0;

    // 전경 레이어는 두 겹으로 나눠 모은다.
    //   Self : 중심 픽셀과 같은 정도로 흐린 샘플(중심 자신 포함) — 중심 표면 자체의 번짐
    //   Fg   : 중심보다 확연히 더 흐린(= 더 가까운) 전경 샘플 — 중심 위를 덮는 앞 물체의 번짐
    // 마지막에 Fg를 Self 위에 over로 합친다. 한 겹으로 면적 가중 평균하면, 초점면 바로 앞의 바닥처럼
    // CoC가 작은 중심 픽셀이 (R/c)² 가중치로 앞 물체의 번짐 색을 덮어 버려 초점면 위치에 가로선이 생긴다.
    float CenterNearCoC = max(-CenterCoC, 0.0);

    float3 SelfAccum = float3(0.0, 0.0, 0.0);
    float  SelfWeightSum = 0.0;
    float  SelfCoverageSum = 0.0;

    float3 FgAccum = float3(0.0, 0.0, 0.0);
    float  FgWeightSum = 0.0;
    float  FgCoverageSum = 0.0;

    int    TotalSamples = 1;

    // 중심 샘플 누적
    {
        float Luma = GetLuma(CenterData.rgb);
        float Boost = 1.0 + HighlightBoost * max(0.0, Luma - HighlightThreshold);

        if (CenterFarCoC > 0.25)
        {
            float W = Boost;
            FarAccum += CenterData.rgb * W;
            FarWeightSum += W;
        }
        if (CenterCoC < -0.25)
        {
            // 면적 정규화 (아래 전경 Gather와 같은 규칙): 샘플 하나가 커널 면적의 1/N을 대표한다
            float AreaW = (DilatedNearCoC * DilatedNearCoC) / max(CenterNearCoC * CenterNearCoC, 0.25);
            float W = Boost * AreaW;
            SelfAccum += CenterData.rgb * W;
            SelfWeightSum += W;
            SelfCoverageSum += AreaW;
        }
    }

    // 배경 Gather (중심 픽셀이 배경일 때만 수행)
    if (CenterFarCoC > 0.25)
    {
        [unroll]
        for (int Ring = 0; Ring < RingCount; ++Ring)
        {
            // 링 반지름을 [Ring + 0.5, Ring + 1] / RingCount 안에서 흔든다. 바깥 링은 커널 끝(1.0)까지 닿는다.
            float RingNorm = ((float)Ring + lerp(0.5, 1.0, RadiusJitter)) / (float)RingCount;
            int NumSamples = SamplesPerRing[Ring];
            float AngleStep = TWO_PI / (float)NumSamples;

            for (int i = 0; i < NumSamples; ++i)
            {
                float Angle = ((float)i + AngleJitter) * AngleStep;
                float2 OffsetHalfPx = ShapeDiaphragm(RingNorm * CenterFarCoC, Angle);
                float SampleDist = length(OffsetHalfPx);

                float4 SampleData = HalfColorCoCTexture.SampleLevel(LinearClampSampler, CenterUV + OffsetHalfPx * HalfTexelSize, 0);
                float SampleCoC = SampleData.a;

                // 배경 샘플만 받는다 (초점 맞은 전경 픽셀의 색이 배경으로 번지지 않게 마스킹)
                // Scatter-as-gather 조건: 샘플의 CoC가 샘플까지의 거리보다 클 때만 기여
                float FarMask = step(0.25, SampleCoC);
                float Overlap = saturate((SampleCoC - SampleDist + 0.75) / 1.5);
                // 중심보다 훨씬 덜 흐린 앞쪽 배경이 과도하게 침범하는 것을 완화
                float DepthWeight = saturate(SampleCoC / max(CenterFarCoC, 0.5));

                float Luma = GetLuma(SampleData.rgb);
                float Boost = 1.0 + HighlightBoost * max(0.0, Luma - HighlightThreshold);
                float W = FarMask * Overlap * lerp(0.35, 1.0, DepthWeight) * Boost;

                FarAccum += SampleData.rgb * W;
                FarWeightSum += W;
            }
        }
    }

    // 전경 Gather (주변 타일에 전경 CoC가 있을 때만 수행 -> 전경이 초점면/배경 위로 번짐)
    if (DilatedNearCoC > 0.25)
    {
        [unroll]
        for (int Ring = 0; Ring < RingCount; ++Ring)
        {
            // 링 반지름을 [Ring + 0.5, Ring + 1] / RingCount 안에서 흔든다. 바깥 링은 커널 끝(1.0)까지 닿는다.
            float RingNorm = ((float)Ring + lerp(0.5, 1.0, RadiusJitter)) / (float)RingCount;
            int NumSamples = SamplesPerRing[Ring];
            float AngleStep = TWO_PI / (float)NumSamples;
            TotalSamples += NumSamples;

            for (int i = 0; i < NumSamples; ++i)
            {
                float Angle = ((float)i + AngleJitter) * AngleStep;
                float2 OffsetHalfPx = ShapeDiaphragm(RingNorm * DilatedNearCoC, Angle);
                float SampleDist = length(OffsetHalfPx);

                float4 SampleData = HalfColorCoCTexture.SampleLevel(LinearClampSampler, CenterUV + OffsetHalfPx * HalfTexelSize, 0);
                float SampleNearCoC = max(-SampleData.a, 0.0);

                // 전경 샘플의 CoC 반경이 중심 픽셀에 닿을 때만 기여
                float Overlap = saturate((SampleNearCoC - SampleDist + 0.75) / 1.5);
                if (Overlap > 0.0)
                {
                    // Scatter-as-gather 면적 정규화: CoC 반지름 c인 샘플은 빛을 π c² 면적에 퍼뜨린다.
                    // 커널(반지름 R) 안 N개 샘플이 각각 πR²/N을 대표하므로 가중치는 (R/c)².
                    // → 균일한 전경 내부는 커버리지 1, 실루엣 위는 0.5, CoC만큼 바깥에서 0으로 자연스럽게 떨어진다.
                    float AreaW = (DilatedNearCoC * DilatedNearCoC) / max(SampleNearCoC * SampleNearCoC, 0.25);
                    float Luma = GetLuma(SampleData.rgb);
                    float Boost = 1.0 + HighlightBoost * max(0.0, Luma - HighlightThreshold);
                    float W = Overlap * AreaW * Boost;

                    // 중심보다 CoC가 1 하프 픽셀 이상 크면 앞 물체로 보고 Fg로, 아니면 Self로 (2 하프 픽셀에 걸쳐 부드럽게 나눔)
                    float FgFactor = saturate((SampleNearCoC - CenterNearCoC - 1.0) / 2.0);
                    FgAccum += SampleData.rgb * (W * FgFactor);
                    FgWeightSum += W * FgFactor;
                    FgCoverageSum += Overlap * AreaW * FgFactor;
                    SelfAccum += SampleData.rgb * (W * (1.0 - FgFactor));
                    SelfWeightSum += W * (1.0 - FgFactor);
                    SelfCoverageSum += Overlap * AreaW * (1.0 - FgFactor);
                }
            }
        }
    }

    float3 FarColor = (FarWeightSum > 1e-4) ? (FarAccum / FarWeightSum) : CenterData.rgb;
    float FarAlpha = saturate(FarWeightSum);

    // 커버리지 = 면적 정규화된 샘플 합 / 샘플 수 (겹마다 따로).
    float InvTotal = 1.0 / (float)max(TotalSamples, 1);
    float3 SelfColor = (SelfWeightSum > 1e-4) ? (SelfAccum / SelfWeightSum) : float3(0.0, 0.0, 0.0);
    float3 FgColor = (FgWeightSum > 1e-4) ? (FgAccum / FgWeightSum) : float3(0.0, 0.0, 0.0);
    float SelfAlpha = saturate(SelfCoverageSum * InvTotal);
    float FgAlpha = saturate(FgCoverageSum * InvTotal);

    // premultiplied over: 앞 물체(Fg)가 중심 표면(Self) 위에 온다
    float3 NearPremul = FgColor * FgAlpha + SelfColor * SelfAlpha * (1.0 - FgAlpha);
    float NearAlpha = FgAlpha + SelfAlpha * (1.0 - FgAlpha);

    Out.FarLayer = float4(FarColor * FarAlpha, FarAlpha);
    Out.NearLayer = float4(NearPremul, NearAlpha);
    return Out;
}
