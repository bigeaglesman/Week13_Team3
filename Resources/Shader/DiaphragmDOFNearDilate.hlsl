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

// 타일 1칸 = 하프 해상도 4픽셀. 확장 반경이 너무 커지지 않게 상한을 둔다 (4K 기준 최대 CoC ≈ 12타일).
static const int MAX_DILATE_TILES = 16;
static const float HALF_PIXELS_PER_TILE = 4.0;

// 타일 확장(Dilation): 전경 CoC가 최대 반경(화면 폭 2.5%)만큼 실루엣 바깥으로 번질 수 있게 확장한다.
// 반경은 MaxCoCRadiusPx(풀 해상도 px)에서 구한다. 1920 해상도면 48px → 하프 24px → 6타일.
// 이웃 타일의 CoC가 그 타일까지의 거리보다 작으면 여기까지 번지지 못하므로 넣지 않는다.
float mainPS(VSOutput Input) : SV_Target
{
    int2 TilePixel = int2(Input.Position.xy);
    uint TileW, TileH;
    NearCoCTileTexture.GetDimensions(TileW, TileH);
    int2 MaxCoord = int2(TileW - 1, TileH - 1);

    const float MaxCoCHalfPx = MaxCoCRadiusPx * 0.5;
    const int Radius = min((int)ceil(MaxCoCHalfPx / HALF_PIXELS_PER_TILE), MAX_DILATE_TILES);

    float MaxNearCoC = NearCoCTileTexture.Load(int3(TilePixel, 0));
    [loop]
    for (int dy = -Radius; dy <= Radius; ++dy)
    {
        [loop]
        for (int dx = -Radius; dx <= Radius; ++dx)
        {
            int2 Coord = clamp(TilePixel + int2(dx, dy), int2(0, 0), MaxCoord);
            float NeighborCoC = NearCoCTileTexture.Load(int3(Coord, 0));
            // 타일 사이 가장 가까운 거리(하프 px). 같은 줄의 이웃 타일은 경계가 맞닿아 있으므로 한 칸을 뺀다.
            float TileDistHalfPx = max(length(float2(max(abs(dx) - 1, 0), max(abs(dy) - 1, 0))) * HALF_PIXELS_PER_TILE, 0.0);
            if (NeighborCoC > TileDistHalfPx)
            {
                MaxNearCoC = max(MaxNearCoC, NeighborCoC);
            }
        }
    }
    return MaxNearCoC;
}
