#include "DOF_Common.hlsli"

// Input Textures
Texture2D    g_SceneColorTex : register(t0);   // Full Res
Texture2D    g_SceneDepthTex : register(t1);   // Full Res
Texture2D    g_NearBlurredTex: register(t2);   // 1/2 Res (Near Blur)
Texture2D    g_FarBlurredTex : register(t3);   // 1/2 Res (Far Blur)

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
    float2 _Pad1;
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

    // 3. 현재 픽셀의 CoC 계산 (Far Blur 오클루전용)
    float rawDepth = g_SceneDepthTex.Sample(g_PointClampSample, input.texCoord).r;
    float viewDepth = LinearizeDepth(rawDepth, NearClip, FarClip, IsOrthographic2);
    float CoC = CalculateCoCWithSkyCheck(
        viewDepth, FocalDistance, FocalRegion,
        NearTransitionRegion, FarTransitionRegion,
        MaxNearBlurSize, MaxFarBlurSize, FarClip
    );

    // 4. 블러 텍스처 샘플링 (Premultiplied Alpha)
    float4 nearBlurred = g_NearBlurredTex.Sample(g_LinearClampSample, input.texCoord);
    float4 farBlurred = g_FarBlurredTex.Sample(g_LinearClampSample, input.texCoord);

    // 5. 레이어 합성 (Premultiplied Alpha - BlendFactor 제거)
    // Premultiplied alpha가 이미 블렌딩 처리하므로 별도 마스크 불필요

    // [Layer 1] Background (Far Blur)
    float3 farColor = farBlurred.rgb + sceneColor.rgb * (1.0 - farBlurred.a);

    // [Layer 2] Foreground (Near Blur)
    float3 finalColor = nearBlurred.rgb + farColor * (1.0 - nearBlurred.a);

    output.Color = float4(finalColor, 1.0);

    return output;
}
