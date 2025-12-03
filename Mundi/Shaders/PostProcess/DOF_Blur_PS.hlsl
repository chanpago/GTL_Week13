#include "DOF_Common.hlsli"

// Input Textures (Near 또는 Far 분리 텍스처)
Texture2D    g_InputTex : register(t0);   // Near: DOF[0], Far: DOF[1]

SamplerState g_LinearClampSample : register(s0);
SamplerState g_PointClampSample  : register(s1);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

// Constant Buffer
cbuffer DOFBlurCB : register(b2)
{
    float2 BlurDirection;      // (사용 안함)
    float  BlurRadius;         // pixels (최대 블러 반경)
    int    IsFarField;         // 0 = Near, 1 = Far
}

cbuffer ViewportConstants : register(b10)
{
    float4 ViewportRect;
    float4 ScreenSize;
}

#define PI 3.14159265359
#define COC_THRESHOLD 0.001

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
};

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    // 1. 중앙 샘플 (소스가 이미 Near/Far 분리됨)
    float4 centerSample = g_InputTex.Sample(g_PointClampSample, input.texCoord);
    float3 centerColor = centerSample.rgb;
    float centerCoc = centerSample.a;  // 이미 양수 (절대값)

    // 2. 검색 반경 결정
    float halfBlurRadius = BlurRadius * 0.5;
    float2 texelSize = ScreenSize.zw;

    if (halfBlurRadius < 0.5)
    {
        output.Color = float4(centerColor * centerCoc, centerCoc);
        return output;
    }

    // Inline dilate: 주변 최대 CoC 샘플링 (Tile Dilation 대체)
    float dilateRadius = halfBlurRadius * 0.5;
    float maxCoc = centerCoc;
    maxCoc = max(maxCoc, g_InputTex.Sample(g_LinearClampSample, input.texCoord + float2(dilateRadius, 0) * texelSize).a);
    maxCoc = max(maxCoc, g_InputTex.Sample(g_LinearClampSample, input.texCoord - float2(dilateRadius, 0) * texelSize).a);
    maxCoc = max(maxCoc, g_InputTex.Sample(g_LinearClampSample, input.texCoord + float2(0, dilateRadius) * texelSize).a);
    maxCoc = max(maxCoc, g_InputTex.Sample(g_LinearClampSample, input.texCoord - float2(0, dilateRadius) * texelSize).a);

    // 주변에 블러 대상이 없으면 early out
    if (maxCoc < COC_THRESHOLD)
    {
        output.Color = float4(0, 0, 0, 0);
        return output;
    }

    float pixelRadius = maxCoc * halfBlurRadius;
    pixelRadius = clamp(pixelRadius, 1.0, halfBlurRadius);

    // 3. Ring-based Gather
    float3 accColor = float3(0, 0, 0);
    float accWeight = 0.0;
    float validSampleCount = 0.0;

    const int MAX_RING = 5;

    // Ring 0: 중심
    if (centerCoc > COC_THRESHOLD)
    {
        accColor += centerColor;
        accWeight += 1.0;
        validSampleCount += 1.0;
    }

    // Ring 1~MAX_RING
    [unroll]
    for (int ring = 1; ring <= MAX_RING; ring++)
    {
        float ringRadius = pixelRadius * (float(ring) / float(MAX_RING));
        int sampleCount = ring * 8;
        float angleStep = 2.0 * PI / float(sampleCount);

        for (int s = 0; s < sampleCount; s++)
        {
            float angle = float(s) * angleStep;
            float2 offset = float2(cos(angle), sin(angle)) * ringRadius * texelSize;
            float2 sampleUV = input.texCoord + offset;

            float4 sampleVal = g_InputTex.Sample(g_LinearClampSample, sampleUV);
            float3 sampleColor = sampleVal.rgb;
            float sampleCoc = sampleVal.a;

            // 소스가 이미 분리되어 있으므로 CoC > 0이면 유효한 샘플
            if (sampleCoc < COC_THRESHOLD)
            {
                continue;
            }

            // 깊이 체크: 샘플의 CoC가 나보다 크거나 같아야 번질 수 있음
            if (sampleCoc < centerCoc - 0.05)
            {
                continue;
            }

            // Scatter-as-Gather: 샘플의 블러가 현재 거리까지 도달하는지
            float sampleRadiusPx = sampleCoc * halfBlurRadius;
            float coverage = saturate((sampleRadiusPx - ringRadius + 1.0) / max(sampleRadiusPx, 1.0));

            if (coverage < 0.01)
            {
                continue;
            }

            accColor += sampleColor * coverage;
            accWeight += coverage;
            validSampleCount += 1.0;
        }
    }

    // 5. 출력
    if (accWeight < 0.0001)
    {
        output.Color = float4(0, 0, 0, 0);
        return output;
    }

    float3 avgColor = accColor / accWeight;
    float finalAlpha = saturate(validSampleCount / 32.0);

    output.Color = float4(avgColor * finalAlpha, finalAlpha);
    return output;
}
