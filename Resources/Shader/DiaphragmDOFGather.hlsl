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

    float3 NearAccum = float3(0.0, 0.0, 0.0);
    float  NearWeightSum = 0.0;
    float  NearCoverageSum = 0.0;
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
            float W = Boost;
            NearAccum += CenterData.rgb * W;
            NearWeightSum += W;
            NearCoverageSum += 1.0;
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
                    float Luma = GetLuma(SampleData.rgb);
                    float Boost = 1.0 + HighlightBoost * max(0.0, Luma - HighlightThreshold);
                    float W = Overlap * Boost;
                    NearAccum += SampleData.rgb * W;
                    NearWeightSum += W;
                    NearCoverageSum += Overlap;
                }
            }
        }
    }

    float3 FarColor = (FarWeightSum > 1e-4) ? (FarAccum / FarWeightSum) : CenterData.rgb;
    float FarAlpha = saturate(FarWeightSum);

    float3 NearColor = (NearWeightSum > 1e-4) ? (NearAccum / NearWeightSum) : float3(0.0, 0.0, 0.0);
    // 커버리지 스케일: 전경 실루엣 바깥은 부드럽게 페이드, 전경 내부는 불투명(1.0)
    float NearAlpha = saturate((NearCoverageSum / (float)max(TotalSamples, 1)) * 2.2);
    if (CenterCoC < -1.0)
    {
        NearAlpha = max(NearAlpha, saturate((-CenterCoC - 0.5) / 1.5));
    }

    Out.FarLayer = float4(FarColor * FarAlpha, FarAlpha);
    Out.NearLayer = float4(NearColor * NearAlpha, NearAlpha);
    return Out;
}
