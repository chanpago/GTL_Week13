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
    float FocalRegion;             // m
    float NearTransitionRegion;    // m
    float FarTransitionRegion;     // m

    float MaxNearBlurSize;         // pixels
    float MaxFarBlurSize;          // pixels
    float NearClip;
    float FarClip;

    int IsOrthographic2;
    float3 _Pad0;
}

cbuffer ViewportConstants : register(b10)
{
    float4 ViewportRect;
    float4 ScreenSize;
}

// MRT 출력 (Near/Far 분리)
struct PS_OUTPUT
{
    float4 Near : SV_Target0;  // Near 전용 (RGB + |CoC|, Focus/Far는 투명)
    float4 Far  : SV_Target1;  // Far 전용 (RGB + CoC, Focus/Near는 투명)
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    // 1. Bilinear 다운샘플
    float4 sceneColor = g_SceneColorTex.Sample(g_LinearClampSample, input.texCoord);

    // 2. 중심 깊이에서 CoC 계산
    float rawDepth = g_SceneDepthTex.Sample(g_PointClampSample, input.texCoord).r;
    float linearDepth = LinearizeDepth(rawDepth, NearClip, FarClip, IsOrthographic2);

    float CoC = CalculateCoCWithSkyCheck(
        linearDepth,
        FocalDistance,
        FocalRegion,
        NearTransitionRegion,
        FarTransitionRegion,
        MaxNearBlurSize,
        MaxFarBlurSize,
        FarClip
    );

    // 3. Near/Far 분리 출력
    // 색상은 항상 넣고, alpha만 CoC 기반
    // Near: CoC < 0일 때 alpha = |CoC|, 아니면 0
    // Far: CoC > 0일 때 alpha = CoC, 아니면 0
    // Focus 영역도 색상은 있음 (블러가 번질 때 참조용)
    output.Near = float4(sceneColor.rgb, saturate(-CoC));
    output.Far = float4(sceneColor.rgb, saturate(CoC));

    return output;
}
