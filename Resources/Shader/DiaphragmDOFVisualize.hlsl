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

Texture2D<float> SceneDepthTexture : register(t0);

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

// 장치 깊이(표준 Z, ClearDepth = 1.0) → 부호 있는 CoC 반지름(풀 해상도 px)
// 양수(+) = 배경(Far), 음수(-) = 전경(Near), 0 = 초점면(In-Focus)
float ComputeCoCPixels(float DeviceZ)
{
    float CoCPx;
    float DepthBlurPx;

    if (DeviceZ >= 0.999999)
    {
        // 하늘(무한대): D -> infinity 극한값 사용 (0 나눗셈 방지)
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

float4 mainPS(VSOutput Input) : SV_Target
{
    int2 Pixel = int2(Input.Position.xy);
    float DeviceZ = SceneDepthTexture.Load(int3(Pixel, 0));
    float CoCPx = ComputeCoCPixels(DeviceZ);

    // 정규화된 강도 (0 ~ 1): 0.25px 이내 초점면은 검정, 전경(-)은 초록, 배경(+)은 빨강
    float AbsCoC = abs(CoCPx);
    float Norm = saturate((AbsCoC - 0.25) / max(MaxCoCRadiusPx - 0.25, 1.0));
    // 작은 CoC 변화도 눈으로 확인하기 쉽게 제곱근 커브로 표시
    float Visual = sqrt(Norm);

    float3 Color = float3(0.0, 0.0, 0.0);
    if (CoCPx < -0.25)
    {
        Color = float3(0.0, Visual, 0.0); // 전경(Near) = 초록
    }
    else if (CoCPx > 0.25)
    {
        Color = float3(Visual, 0.0, 0.0); // 배경(Far) = 빨강
    }

    return float4(Color, 1.0);
}
