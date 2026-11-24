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

Texture2D<float> NearCoCTileTexture : register(t0);

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

// 5x5 타일 확장(Dilation): 전경 CoC가 최대 반경(화면 폭 2.5%)만큼 실루엣 바깥으로 번질 수 있게 확장
float mainPS(VSOutput Input) : SV_Target
{
    int2 TilePixel = int2(Input.Position.xy);
    uint TileW, TileH;
    NearCoCTileTexture.GetDimensions(TileW, TileH);
    int2 MaxCoord = int2(TileW - 1, TileH - 1);

    float MaxNearCoC = 0.0;
    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            int2 Coord = clamp(TilePixel + int2(dx, dy), int2(0, 0), MaxCoord);
            MaxNearCoC = max(MaxNearCoC, NearCoCTileTexture.Load(int3(Coord, 0)));
        }
    }
    return MaxNearCoC;
}
