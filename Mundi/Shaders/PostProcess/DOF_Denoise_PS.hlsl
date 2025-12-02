#include "DOF_Common.hlsli"

// DOF Denoise Pass
// 지터링으로 생긴 노이즈를 제거하는 가벼운 3x3 가우시안 블러
// DOF Blur Pass 결과(Near/Far)에 적용

Texture2D    g_InputTex : register(t0);
SamplerState g_LinearClampSample : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer ViewportConstants : register(b10)
{
    float4 ViewportRect;
    float4 ScreenSize;  // xy: size, zw: texel size
}

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
};

// 3x3 Gaussian Kernel (sigma ≈ 0.85)
// 중앙 가중치가 높아 원본 디테일 보존 + 노이즈만 부드럽게
static const float KERNEL[3][3] = {
    { 1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0 },
    { 2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0 },
    { 1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0 }
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    float2 texelSize = ScreenSize.zw;

    float4 accColor = float4(0, 0, 0, 0);

    // 3x3 Gaussian Convolution
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2(x, y) * texelSize;
            float2 sampleUV = input.texCoord + offset;

            float4 sampleColor = g_InputTex.Sample(g_LinearClampSample, sampleUV);
            float weight = KERNEL[y + 1][x + 1];

            accColor += sampleColor * weight;
        }
    }

    // Premultiplied Alpha 유지
    output.Color = accColor;

    return output;
}
