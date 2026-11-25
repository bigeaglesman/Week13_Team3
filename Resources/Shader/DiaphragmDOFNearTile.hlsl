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

// 1/8 풀 해상도 타일 (= 하프 해상도 4x4 블록)의 최대 전경 CoC 크기(하프 픽셀 단위, >= 0)
float mainPS(VSOutput Input) : SV_Target
{
    int2 TilePixel = int2(Input.Position.xy);
    int2 HalfBase = TilePixel * 4;

    uint HalfW, HalfH;
    HalfColorCoCTexture.GetDimensions(HalfW, HalfH);
    int2 MaxCoord = int2(HalfW - 1, HalfH - 1);

    float MaxNearCoC = 0.0;
    [unroll]
    for (int y = 0; y < 4; ++y)
    {
        [unroll]
        for (int x = 0; x < 4; ++x)
        {
            int2 Coord = min(HalfBase + int2(x, y), MaxCoord);
            float CoCHalfPx = HalfColorCoCTexture.Load(int3(Coord, 0)).a;
            MaxNearCoC = max(MaxNearCoC, max(-CoCHalfPx, 0.0));
        }
    }
    return MaxNearCoC;
}
