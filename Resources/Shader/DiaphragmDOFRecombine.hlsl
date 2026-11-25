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

Texture2D<float4> SceneColorCopyTexture : register(t0);
Texture2D<float>  SceneDepthTexture     : register(t1);
Texture2D<float4> HalfFarFiltered       : register(t2);
Texture2D<float4> HalfNearFiltered      : register(t3);
SamplerState LinearClampSampler         : register(s0);

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

// 풀 해상도 최종 합성:
// 1) 초점 맞은 원본(선형)과 흐린 배경(HalfFar)을 풀 해상도 CoC로 혼합
// 2) 흐린 전경(HalfNear)을 커버리지(a)로 최상단에 덮어 전경이 배경 위로 번지게 합성
// 3) 감마 재인코딩 후 Targets.SceneColor에 출력
float4 mainPS(VSOutput Input) : SV_Target
{
    int2 Pixel = int2(Input.Position.xy);
    float2 UV = Input.Position.xy * float2(InvFullWidth, InvFullHeight);

    float4 SceneSample = SceneColorCopyTexture.Load(int3(Pixel, 0));
    float3 SharpLinear = pow(max(SceneSample.rgb, 0.0), DisplayGamma);

    float DeviceZ = SceneDepthTexture.Load(int3(Pixel, 0));
    float CoCPx = ComputeCoCPixels(DeviceZ);

    float4 FarSample = HalfFarFiltered.SampleLevel(LinearClampSampler, UV, 0);
    float4 NearSample = HalfNearFiltered.SampleLevel(LinearClampSampler, UV, 0);

    // 레이어는 premultiplied(rgb = 색 × a)로 들어온다. bilinear 업샘플 뒤에 a로 나눠 원래 색을 되찾는다.
    // a = 0인 텍셀(초점 맞은 물체 자리)은 색이 0이라 경계 너머로 색이 번지지 않는다.
    float3 FarColor = FarSample.rgb / max(FarSample.a, 1e-4);

    // 배경 합성: CoC가 0.25px 이하이면 선명한 원본 100%, 2.0px 이상이면 하프 배경 100%
    float FarBlend = smoothstep(0.25, 2.0, CoCPx) * saturate(FarSample.a);
    float3 ResultLinear = lerp(SharpLinear, FarColor, FarBlend);

    // 흐린 전경 물체 내부: 그 뒤의 배경은 가려져 있어 알 수 없으므로, 바탕을 선명한 원본 대신 흐린 전경 색으로 바꾼다.
    // (커버리지가 실루엣 근처에서 0.5까지 내려가도 선명한 체커가 비쳐 보이지 않게)
    // 바꾸는 정도는 픽셀 자신의 CoC로 넓은 구간에 걸쳐 서서히 올린다.
    // 좁은 구간(1.5px)이면 초점면을 가로지르는 바닥 같은 연속면에서, 초점면 바로 앞 픽셀부터 바탕이
    // 주변 전경의 번짐 색으로 한꺼번에 바뀌어 가로선이 생긴다 (초점 거리를 바꾸면 선이 위아래로 이동).
    float NearInsideRampPx = max(MaxCoCRadiusPx * 0.25, 2.0);
    float NearInside = smoothstep(0.5, 0.5 + NearInsideRampPx, -CoCPx);
    if (NearSample.a > 1e-3)
    {
        float3 NearUnpremul = NearSample.rgb / NearSample.a;
        ResultLinear = lerp(ResultLinear, NearUnpremul, NearInside);
    }

    // 전경 합성(premultiplied over): 전경 레이어를 최상단에 덮어 실루엣 바깥 번짐 표현
    float NearBlend = saturate(NearSample.a);
    ResultLinear = ResultLinear * (1.0 - NearBlend) + NearSample.rgb;

    float3 FinalColor = pow(max(ResultLinear, 0.0), 1.0 / DisplayGamma);
    return float4(FinalColor, SceneSample.a);
}
