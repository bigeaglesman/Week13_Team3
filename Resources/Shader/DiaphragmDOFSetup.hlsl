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

Texture2D<float4> SceneColorTexture : register(t0);
Texture2D<float>  SceneDepthTexture : register(t1);

struct VSOutput
{
    float4 Position : SV_Position;
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

float3 LinearizeColor(float3 Color)
{
    return pow(max(Color, 0.0), DisplayGamma);
}

// 장치 깊이(표준 Z, ClearDepth = 1.0) → 부호 있는 CoC 반지름(풀 해상도 px)
float ComputeCoCPixels(float DeviceZ)
{
    float CoCPx;
    float DepthBlurPx;

    if (DeviceZ >= 0.999999)
    {
        CoCPx = CoCScale;
        DepthBlurPx = DepthBlurRadiusPx;
    }
    else
    {
        float Denom = max(FarClip - DeviceZ * (FarClip - NearClip), 1e-5);
        float SceneDepthM = (NearClip * FarClip) / Denom;
        CoCPx = CoCScale * (1.0 - FocalDistance / max(SceneDepthM, 1e-4));
        DepthBlurPx = DepthBlurRadiusPx * (1.0 - exp2(-(SceneDepthM * 0.001) / DepthBlurAmountKm));
    }

    if (CoCPx > 0.0)
    {
        CoCPx = max(CoCPx, DepthBlurPx);
    }

    return clamp(CoCPx, -MaxCoCRadiusPx, MaxCoCRadiusPx);
}

// 하프 해상도 1픽셀 = 풀 해상도 2x2 블록. Karis 평균으로 깜빡임(fireflies)을 억제하고
// 전경 실루엣 보존을 위해 4샘플의 부호 있는 CoC를 조합한다 (하프 픽셀 반지름으로 저장).
float4 mainPS(VSOutput Input) : SV_Target
{
    int2 HalfPixel = int2(Input.Position.xy);
    int2 FullBase = HalfPixel * 2;

    uint FullW, FullH;
    SceneColorTexture.GetDimensions(FullW, FullH);
    int2 MaxCoord = int2(FullW - 1, FullH - 1);

    int2 P0 = min(FullBase + int2(0, 0), MaxCoord);
    int2 P1 = min(FullBase + int2(1, 0), MaxCoord);
    int2 P2 = min(FullBase + int2(0, 1), MaxCoord);
    int2 P3 = min(FullBase + int2(1, 1), MaxCoord);

    float3 C0 = LinearizeColor(SceneColorTexture.Load(int3(P0, 0)).rgb);
    float3 C1 = LinearizeColor(SceneColorTexture.Load(int3(P1, 0)).rgb);
    float3 C2 = LinearizeColor(SceneColorTexture.Load(int3(P2, 0)).rgb);
    float3 C3 = LinearizeColor(SceneColorTexture.Load(int3(P3, 0)).rgb);

    float CoC0 = ComputeCoCPixels(SceneDepthTexture.Load(int3(P0, 0)));
    float CoC1 = ComputeCoCPixels(SceneDepthTexture.Load(int3(P1, 0)));
    float CoC2 = ComputeCoCPixels(SceneDepthTexture.Load(int3(P2, 0)));
    float CoC3 = ComputeCoCPixels(SceneDepthTexture.Load(int3(P3, 0)));

    // Karis 평균: 1 / (1 + Luma) 가중치로 TAA 없는 환경의 하이라이트 깜빡임 완화
    float W0 = 1.0 / (1.0 + GetLuma(C0));
    float W1 = 1.0 / (1.0 + GetLuma(C1));
    float W2 = 1.0 / (1.0 + GetLuma(C2));
    float W3 = 1.0 / (1.0 + GetLuma(C3));

    float3 AvgColor = (C0 * W0 + C1 * W1 + C2 * W2 + C3 * W3) / max(W0 + W1 + W2 + W3, 1e-5);

    // 전경(음수 CoC)이 하나라도 강하면 전경 CoC를 우선 보존하고, 아니면 평균 CoC 사용
    float MinCoC = min(min(CoC0, CoC1), min(CoC2, CoC3));
    float AvgCoC = 0.25 * (CoC0 + CoC1 + CoC2 + CoC3);
    float FinalCoCFullPx = (MinCoC < -0.5) ? MinCoC : AvgCoC;

    // 하프 해상도 패스에서 쓰기 쉽게 하프 픽셀 단위 반지름으로 저장
    return float4(AvgColor, FinalCoCFullPx * 0.5);
}
