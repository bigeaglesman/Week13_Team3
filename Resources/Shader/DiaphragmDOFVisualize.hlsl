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

    // 시각화 전용
    int VisualizeMode;      // 1 = Layers (씬 위 오버레이), 2 = CoC Only
    float InFocusCoCPx;     // 이 반지름 이하의 CoC는 "선명함(DoF 범위)"
    float GridSpacingM;     // 등거리선 간격 (0 = 끔)
    float VisualizePad;
};

Texture2D<float>  SceneDepthTexture : register(t0);
Texture2D<float4> SceneColorTexture : register(t1);

static const float3 NearColor       = float3(0.10, 0.95, 0.20); // 근경 흐림
static const float3 FarColor        = float3(0.15, 0.45, 1.00); // 원경 흐림
static const float3 FocalPlaneColor = float3(1.00, 0.90, 0.00); // 초점면
static const float3 DoFEdgeColor    = float3(0.00, 1.00, 1.00); // DoF 범위 경계 (앞/뒤)

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

// 장치 깊이(표준 Z, ClearDepth = 1.0) → 선형 깊이 (m)
float LinearizeDepth(float DeviceZ)
{
    float Denom = max(FarClip - DeviceZ * (FarClip - NearClip), 1e-5);
    return (NearClip * FarClip) / Denom;
}

// 선형 깊이 → 부호 있는 CoC 반지름(풀 해상도 px). 양수 = 원경, 음수 = 근경. (DiaphragmDOFSetup과 같은 식)
float ComputeCoCPixels(float SceneDepthM, bool bSky)
{
    float CoCPx;
    float DepthBlurPx;

    if (bSky)
    {
        CoCPx = CoCScale;
        DepthBlurPx = DepthBlurRadiusPx;
    }
    else
    {
        CoCPx = CoCScale * (1.0 - FocalDistance / max(SceneDepthM, 1e-4));
        DepthBlurPx = DepthBlurRadiusPx * (1.0 - exp2(-(SceneDepthM * 0.001) / DepthBlurAmountKm));
    }

    if (CoCPx > 0.0)
    {
        CoCPx = max(CoCPx, DepthBlurPx);
    }

    return clamp(CoCPx, -MaxCoCRadiusPx, MaxCoCRadiusPx);
}

// Value가 Target에 화면상 약 WidthPx 픽셀 이내로 가까우면 1 (등고선 그리기)
float ContourLine(float Value, float Target, float PixelFootprint, float WidthPx)
{
    float Dist = abs(Value - Target) / max(PixelFootprint, 1e-6);
    return 1.0 - smoothstep(WidthPx * 0.5, WidthPx * 0.5 + 1.0, Dist);
}

float4 mainPS(VSOutput Input) : SV_Target
{
    int2 Pixel = int2(Input.Position.xy);
    float DeviceZ = SceneDepthTexture.Load(int3(Pixel, 0));
    bool bSky = DeviceZ >= 0.999999;

    // fwidth는 분기 전에 계산해야 한다 (픽셀 쿼드 단위 미분)
    float SceneDepthM = LinearizeDepth(DeviceZ);
    float DepthFootprint = fwidth(SceneDepthM);

    float CoCPx = ComputeCoCPixels(SceneDepthM, bSky);
    float AbsCoC = abs(CoCPx);
    float Norm = saturate((AbsCoC - InFocusCoCPx) / max(MaxCoCRadiusPx - InFocusCoCPx, 1.0));
    // 작은 CoC 변화도 눈에 띄도록 제곱근 커브
    float Visual = sqrt(Norm);

    // ---- CoC Only: 씬 없이 CoC 크기만 ----
    if (VisualizeMode == 2)
    {
        float3 Color = float3(0.0, 0.0, 0.0);
        if (CoCPx < -InFocusCoCPx)      Color = NearColor * Visual;
        else if (CoCPx > InFocusCoCPx)  Color = FarColor * Visual;
        return float4(Color, 1.0);
    }

    // ---- Layers: 원본 씬 위에 영역/경계선을 겹쳐 표시 ----
    float3 Scene = SceneColorTexture.Load(int3(Pixel, 0)).rgb;
    float Luma = dot(Scene, float3(0.299, 0.587, 0.114));

    float3 Color;
    if (AbsCoC <= InFocusCoCPx)
    {
        // DoF 범위(선명): 원본 색 그대로
        Color = Scene;
    }
    else
    {
        // 흐려지는 영역: 흑백으로 낮춘 씬에 근경/원경 색을 CoC 크기만큼 섞는다
        float3 Gray = Luma.xxx * 0.6;
        float3 Tint = CoCPx < 0.0 ? NearColor : FarColor;
        Color = lerp(Gray, Tint * (0.35 + 0.65 * Luma), 0.35 + 0.55 * Visual);
    }

    // 깊이가 끊기는 실루엣(물체와 하늘 경계 등)에서는 등고선을 그리지 않는다
    bool bContinuous = !bSky && DepthFootprint < SceneDepthM * 0.25;
    if (bContinuous)
    {
        // 1m / 5m 등거리선
        if (GridSpacingM > 0.0)
        {
            float Nearest1 = round(SceneDepthM / GridSpacingM) * GridSpacingM;
            float Nearest5 = round(SceneDepthM / (GridSpacingM * 5.0)) * (GridSpacingM * 5.0);
            float Line1 = ContourLine(SceneDepthM, Nearest1, DepthFootprint, 1.0);
            float Line5 = ContourLine(SceneDepthM, Nearest5, DepthFootprint, 2.0);
            Color = lerp(Color, float3(1.0, 1.0, 1.0), max(Line1 * 0.25, Line5 * 0.6));
        }

        // DoF 범위의 앞/뒤 경계: |CoC| == InFocusCoCPx 가 되는 거리
        float Ratio = InFocusCoCPx / max(CoCScale, 1e-4);
        float DoFNear = FocalDistance / (1.0 + Ratio);
        float EdgeLine = ContourLine(SceneDepthM, DoFNear, DepthFootprint, 2.0);
        if (Ratio < 1.0)
        {
            float DoFFar = FocalDistance / (1.0 - Ratio);
            EdgeLine = max(EdgeLine, ContourLine(SceneDepthM, DoFFar, DepthFootprint, 2.0));
        }
        Color = lerp(Color, DoFEdgeColor, EdgeLine);

        // 초점면: 정확히 FocalDistance인 곳
        float FocalLine = ContourLine(SceneDepthM, FocalDistance, DepthFootprint, 3.0);
        Color = lerp(Color, FocalPlaneColor, FocalLine);
    }

    return float4(Color, 1.0);
}
