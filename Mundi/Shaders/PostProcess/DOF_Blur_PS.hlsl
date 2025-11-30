#include "DOF_Common.hlsli"

// Input Textures
Texture2D    g_InputTex : register(t0);
Texture2D    g_TileTex  : register(t1);   // Dilated Tile (Min/Max CoC)

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
    float2 BlurDirection;      // (사용 안함 - Ring은 2D)
    float  BlurRadius;         // pixels (최대 블러 반경)
    int    IsFarField;         // 1 = Far Field, 0 = Near Field
}

cbuffer ViewportConstants : register(b10)
{
    float4 ViewportRect;
    float4 ScreenSize;
}

#define TILE_SIZE 8
#define PI 3.14159265359

// 출력
struct PS_OUTPUT
{
    float4 Color : SV_Target0;
};

// Ring-based 2D DOF Blur
// 1패스로 원형 보케 생성
PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    // 1. 중앙 샘플 (자기 자신)
    float4 centerSample = g_InputTex.Sample(g_PointClampSample, input.texCoord);
    float3 centerColor = centerSample.rgb;
    float centerCoc = centerSample.a;

    // Early exit for copy pass (BlurRadius = 0)
    if (BlurRadius < 0.01)
    {
        output.Color = centerSample;
        return output;
    }

    // 2. Tile에서 Max CoC 가져오기 (Point Sampling)
    float2 pixelPos = input.texCoord * ScreenSize.xy;
    float2 tileCount = ScreenSize.xy / TILE_SIZE;
    int2 tileIdx = int2(pixelPos / TILE_SIZE);
    tileIdx = clamp(tileIdx, int2(0, 0), int2(tileCount) - 1);
    float4 tileData = g_TileTex.Load(int3(tileIdx, 0));
    float tileMaxCoc = (IsFarField == 1) ? tileData.x : tileData.y;

    // 3. Early Out: 주변에 흐린 게 하나도 없으면 원본 리턴
    if (centerCoc < 0.01 && tileMaxCoc < 0.01)
    {
        output.Color = centerSample;
        return output;
    }

    // 4. 검색 반경 결정 (100% tileMaxCoc 사용)
    // CoC는 0~1 정규화
    float halfBlurRadius = BlurRadius * 0.25;
    float searchRadius = max(centerCoc, tileMaxCoc);  // 0~1 정규화
    float pixelRadius = searchRadius * halfBlurRadius;  // Half Res 픽셀
    pixelRadius = clamp(pixelRadius, 1.0, halfBlurRadius);

    // 5. Ring-based 2D Gather
    float2 texelSize = ScreenSize.zw;
    float3 accColor = float3(0, 0, 0);
    float accCoc = 0.0;  // CoC도 블러 (확산)
    float accWeight = 0.0;

    // Ring 설정
    const int MAX_RING = 5;  // Ring 0(중심) + Ring 1~5 = 121 샘플

    // Ring 0: 중심 (1 샘플)
    {
        float weight = centerCoc + 0.01;  // 최소 가중치 보장
        accColor += centerColor * weight;
        accCoc += centerCoc * weight;
        accWeight += weight;
    }

    // Ring 1~MAX_RING: 원형 샘플링
    // 총 샘플 수: 1 + 8 + 16 + 24 + 32 + 40 = 121 샘플
    [unroll]
    for (int ring = 1; ring <= MAX_RING; ring++)
    {
        float ringRadius = pixelRadius * (float(ring) / float(MAX_RING));
        int sampleCount = ring * 8;  // Ring 1=8, Ring 2=16, Ring 3=24, Ring 4=32
        float angleStep = 2.0 * PI / float(sampleCount);

        for (int s = 0; s < sampleCount; s++)
        {
            float angle = float(s) * angleStep;
            float2 offset = float2(cos(angle), sin(angle)) * ringRadius * texelSize;
            float2 sampleUV = input.texCoord + offset;

            float4 sampleVal = g_InputTex.Sample(g_LinearClampSample, sampleUV);
            float3 sampleColor = sampleVal.rgb;
            float sampleCoc = sampleVal.a;

            // Scatter-as-Gather: 샘플의 CoC가 현재 위치까지 도달하는지 검사
            // sampleCoc는 0~1 정규화, Half Res 픽셀로 변환
            float sampleRadiusPx = sampleCoc * halfBlurRadius;

            // 샘플의 블러 반경이 현재 거리까지 도달하는 정도
            float coverage = saturate((sampleRadiusPx - ringRadius + 1.0) / max(sampleRadiusPx, 0.01));

            // 거리 기반 Gaussian 감쇠 (부드러운 보케)
            float sigma = pixelRadius * 0.5;
            float distanceFalloff = exp(-(ringRadius * ringRadius) / (2.0 * sigma * sigma));

            // 가중치 = Coverage * 거리감쇠 * CoC
            float weight = coverage * distanceFalloff * sampleCoc;
            accColor += sampleColor * weight;
            accCoc += sampleCoc * weight;  // CoC도 누적
            accWeight += weight;
        }
    }

    // 6. 정규화 및 출력
    output.Color.rgb = accColor / max(accWeight, 0.0001);
    output.Color.a = accCoc / max(accWeight, 0.0001);  // 블러된 CoC (확산됨)

    return output;
}
