#include "DOF_Common.hlsli"

// Input Texture
Texture2D    g_InputTex : register(t0);
SamplerState g_LinearClampSample : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

// Constant Buffer
cbuffer DOFBlurCB : register(b2)
{
    float2 BlurDirection;      // (1,0) for horizontal, (0,1) for vertical
    float  BlurRadius;         // pixels (최대 블러 반경)
    float  _Pad0;
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

// Separable Gaussian Blur with CoC Weighting (Intensity Leakage 방지)
PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    // 1. 중앙 샘플
    float4 centerSample = g_InputTex.Sample(g_LinearClampSample, input.texCoord);
    float centerCoc = centerSample.a;

    // 2. CoC가 0이면 블러 불필요 (선명 영역)
    if (centerCoc < 0.001)
    {
        output.Color = centerSample;
        return output;
    }

    // 3. Variable Kernel Size (1/4 해상도 고려해서 스케일 다운)
    float kernelRadius = centerCoc * BlurRadius * 0.25;
    kernelRadius = max(kernelRadius, 0.5);

    // 4. Gaussian Blur with CoC Weighting
    float2 texelSize = ScreenSize.zw;
    float2 blurStep = BlurDirection * texelSize;

    float3 colorSum = float3(0, 0, 0);
    float weightSum = 0.0;

    // Center (CoC 가중치 적용)
    float centerWeight = GAUSSIAN_WEIGHTS_13[0] * centerCoc;
    colorSum += centerSample.rgb * centerWeight;
    weightSum += centerWeight;

    // ±1 ~ ±6
    [unroll]
    for (int i = 1; i < 7; i++)
    {
        float offset = float(i) * (kernelRadius / 6.0);
        float2 sampleOffset = blurStep * offset;
        float baseWeight = GAUSSIAN_WEIGHTS_13[i];

        // Positive direction
        float4 samplePos = g_InputTex.Sample(g_LinearClampSample, input.texCoord + sampleOffset);
        float weightPos = baseWeight * samplePos.a;  // CoC 가중치
        colorSum += samplePos.rgb * weightPos;
        weightSum += weightPos;

        // Negative direction
        float4 sampleNeg = g_InputTex.Sample(g_LinearClampSample, input.texCoord - sampleOffset);
        float weightNeg = baseWeight * sampleNeg.a;  // CoC 가중치
        colorSum += sampleNeg.rgb * weightNeg;
        weightSum += weightNeg;
    }

    // 5. Normalize (CoC=0인 샘플은 자동으로 제외됨)
    float3 blurredColor = (weightSum > 0.001) ? (colorSum / weightSum) : centerSample.rgb;
    output.Color = float4(blurredColor, centerCoc);

    return output;
}
