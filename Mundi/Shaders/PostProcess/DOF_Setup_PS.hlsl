#include "DOF_Common.hlsli"

// Input Textures
Texture2D    g_SceneColorTex : register(t0);
Texture2D    g_SceneDepthTex : register(t1);

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

cbuffer DOFSetupCB : register(b2)
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
}

cbuffer ViewportConstants : register(b10)
{
    float4 ViewportRect;
    float4 ScreenSize;
}

// MRT 출력 (Far Field, Near Field)
struct PS_OUTPUT
{
    float4 FarField  : SV_Target0;
    float4 NearField : SV_Target1;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    // 1. 씬 컬러 다운샘플 (1/4 해상도이므로 4x4 샘플링)
    float2 fullResTexelSize = ScreenSize.zw;  // 1.0 / ScreenSize
    float4 sceneColor = DownsampleSceneColor4x4(g_SceneColorTex, g_LinearClampSample, input.texCoord, fullResTexelSize);

    // 2. 깊이 샘플링 (Point 샘플러로 중앙값)
    float rawDepth = g_SceneDepthTex.Sample(g_PointClampSample, input.texCoord).r;

    // 3. 선형 깊이로 변환 (View-space, m 단위)
    float viewDepth = LinearizeDepth(rawDepth, NearClip, FarClip, IsOrthographic2);

    // 4. CoC 계산
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

    // 5. Near/Far 분리 (순수 저장, Blur에서 CoC 가중치 처리)
    float farCoc = max(CoC, 0.0);
    float nearCoc = max(-CoC, 0.0);

    output.FarField = float4(sceneColor.rgb, farCoc);
    output.NearField = float4(sceneColor.rgb, nearCoc);

    return output;
}
