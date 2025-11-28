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

    // 2. Depth 샘플링 및 CoC 계산
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

    // 3. CoC가 0이면 원본 그대로 (선명 영역)
    if (abs(CoC) < 0.001)
    {
        output.Color = sceneColor;
        return output;
    }

    // 4. Blurred Field 샘플링 (1/4 해상도이므로 Linear Sampler로 업샘플)
    float4 farField = g_FarFieldTex.Sample(g_LinearClampSample, input.texCoord);
    float4 nearField = g_NearFieldTex.Sample(g_LinearClampSample, input.texCoord);

    // 5. CoC에 따라 합성
    float4 finalColor;

    if (CoC > 0.0)
    {
        // 원경 (Far Field) - 초점보다 멀리
        float blendFactor = saturate(CoC);  // 0~1
        finalColor = lerp(sceneColor, farField, blendFactor);
    }
    else  // CoC < 0.0
    {
        // 근경 (Near Field) - 초점보다 가까이
        float blendFactor = saturate(abs(CoC));  // 0~1
        finalColor = lerp(sceneColor, nearField, blendFactor);
    }

    output.Color = finalColor;

    return output;
}
