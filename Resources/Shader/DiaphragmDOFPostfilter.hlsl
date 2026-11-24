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

Texture2D<float4> HalfFarTexture  : register(t0);
Texture2D<float4> HalfNearTexture : register(t1);
SamplerState LinearClampSampler   : register(s0);

struct VSOutput
{
    float4 Position : SV_Position;
};

struct PSOutput
{
    float4 FarFiltered  : SV_Target0;
    float4 NearFiltered : SV_Target1;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    VSOutput Output;
    Output.Position = float4(UV * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return Output;
}

// 3x3 텐트 필터(하프 해상도)로 Gather 샘플 사이의 미세 구멍과 TAA 부재 노이즈를 부드럽게 정리한다.
PSOutput mainPS(VSOutput Input)
{
    float2 HalfTexelSize = float2(InvHalfWidth, InvHalfHeight);
    float2 UV = Input.Position.xy * HalfTexelSize;

    // 4개 대각선 절반 오프셋 bilinear 탭 = 효율적인 3x3 텐트 필터
    float2 D = HalfTexelSize * 0.5;

    float4 FarSum =
        HalfFarTexture.SampleLevel(LinearClampSampler, UV + float2(-D.x, -D.y), 0) +
        HalfFarTexture.SampleLevel(LinearClampSampler, UV + float2( D.x, -D.y), 0) +
        HalfFarTexture.SampleLevel(LinearClampSampler, UV + float2(-D.x,  D.y), 0) +
        HalfFarTexture.SampleLevel(LinearClampSampler, UV + float2( D.x,  D.y), 0);

    float4 NearSum =
        HalfNearTexture.SampleLevel(LinearClampSampler, UV + float2(-D.x, -D.y), 0) +
        HalfNearTexture.SampleLevel(LinearClampSampler, UV + float2( D.x, -D.y), 0) +
        HalfNearTexture.SampleLevel(LinearClampSampler, UV + float2(-D.x,  D.y), 0) +
        HalfNearTexture.SampleLevel(LinearClampSampler, UV + float2( D.x,  D.y), 0);

    PSOutput Out;
    Out.FarFiltered = FarSum * 0.25;
    Out.NearFiltered = NearSum * 0.25;
    return Out;
}
