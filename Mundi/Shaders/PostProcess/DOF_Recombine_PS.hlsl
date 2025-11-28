#include "DOF_Common.hlsli"

// Input Textures
Texture2D    g_SceneColorTex : register(t0);  // Full Res
Texture2D    g_SceneDepthTex : register(t1);  // Full Res
Texture2D    g_FarFieldTex   : register(t2);  // 1/4 Res (Blurred)
Texture2D    g_NearFieldTex  : register(t3);  // 1/4 Res (Blurred)

SamplerState g_LinearClampSample : register(s0);
SamplerState g_PointClampSample  : register(s1);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

// Constant Buffers
cbuffer PostProcessCB : register(b0)
{
    float Near;
    float Far;
    int IsOrthographic;
    float Padding0;
}

cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
}

cbuffer DOFRecombineCB : register(b2)
{
    float FocalDistance;           // m (meters)
    float Fstop;
    float SensorWidth;             // m (e.g., 0.024 = 24mm)
    float FocalRegion;             // m

    float NearTransitionRegion;    // m
    float FarTransitionRegion;     // m
    float MaxNearBlurSize;         // pixels
    float MaxFarBlurSize;          // pixels

    float NearClip;
    float FarClip;
    int IsOrthographic2;
    float _Pad0;

    float2 ViewRectMinUV;          // ViewRect 시작 UV (게임 영역)
    float2 ViewRectMaxUV;          // ViewRect 끝 UV (게임 영역)
}

cbuffer ViewportConstants : register(b10)
{
    float4 ViewportRect;
    float4 ScreenSize;
}

// 출력
struct PS_OUTPUT
{
    float4 Color : SV_Target0;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    // 1. 원본 씬 컬러 샘플링
    float4 sceneColor = g_SceneColorTex.Sample(g_PointClampSample, input.texCoord);

    // 2. ViewRect 영역 체크 (게임 영역만 DOF 적용, UI 영역은 원본)
    bool inViewRect = all(input.texCoord >= ViewRectMinUV) && all(input.texCoord <= ViewRectMaxUV);
    if (!inViewRect)
    {
        output.Color = sceneColor;
        return output;
    }

    // 3. Depth 샘플링 및 CoC 계산
    float rawDepth = g_SceneDepthTex.Sample(g_PointClampSample, input.texCoord).r;
    float viewDepth = LinearizeDepth(rawDepth, NearClip, FarClip, IsOrthographic2);

    float CoC = CalculateCoC(
        viewDepth,
        FocalDistance,
        Fstop,
        SensorWidth,
        FocalRegion,
        NearTransitionRegion,
        FarTransitionRegion,
        MaxNearBlurSize,
        MaxFarBlurSize
    );

    // 4. CoC가 0이면 원본 그대로 (선명 영역)
    if (abs(CoC) < 0.001)
    {
        output.Color = sceneColor;
        return output;
    }

    // 5. Blurred Field 샘플링 (Blur에서 이미 정규화됨)
    float4 farField = g_FarFieldTex.Sample(g_LinearClampSample, input.texCoord);
    float4 nearField = g_NearFieldTex.Sample(g_LinearClampSample, input.texCoord);

    // 6. CoC에 따라 합성
    float4 finalColor;

    if (CoC > 0.0)
    {
        // 원경 (Far Field)
        float blendFactor = saturate(CoC);
        finalColor = lerp(sceneColor, float4(farField.rgb, 1.0), blendFactor);
    }
    else  // CoC < 0.0
    {
        // 근경 (Near Field)
        float blendFactor = saturate(abs(CoC));
        finalColor = lerp(sceneColor, float4(nearField.rgb, 1.0), blendFactor);
    }

    output.Color = finalColor;

    return output;
}
