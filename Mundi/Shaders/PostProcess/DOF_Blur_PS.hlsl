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
    int    IsFarField;         // 0 = Near (Foreground), 1 = Far (Background)
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
    float centerCocSigned = centerSample.a;  // Near: 음수, Far: 양수
    float centerCoc = abs(centerCocSigned);  // 블러 크기는 절대값

    // 2. Tile에서 Max CoC 가져오기 (Near/Far 분리)
    // tileData: x=MaxNearCoC, y=MaxFarCoC
    float2 pixelPos = input.texCoord * ScreenSize.xy;
    float2 tileCount = ScreenSize.xy / TILE_SIZE;
    int2 tileIdx = int2(pixelPos / TILE_SIZE);
    tileIdx = clamp(tileIdx, int2(0, 0), int2(tileCount) - 1);
    float4 tileData = g_TileTex.Load(int3(tileIdx, 0));
    float tileMaxCoc = (IsFarField == 0) ? tileData.x : tileData.y;

    // In-Focus 임계값 (이 이하는 선명 영역)
    const float COC_THRESHOLD = 0.01;

    // 4. 검색 반경 결정
    // CoC는 0~1 정규화, BlurRadius는 Full Res 픽셀
    float halfBlurRadius = BlurRadius * 0.5;  // Half Res로 변환

    // Edge bleeding
    float searchRadius = max(centerCoc, tileMaxCoc); 
    float pixelRadius = searchRadius * halfBlurRadius;  // Half Res 픽셀
    pixelRadius = clamp(pixelRadius, 1.0, halfBlurRadius);

    // 5. Ring-based 2D Gather (단순화)
    float2 texelSize = ScreenSize.zw;
    float3 accColor = float3(0, 0, 0);
    float accWeight = 0.0;
    float validSampleCount = 0.0;

    // Ring 설정
    const int MAX_RING = 5;

    // Ring 0: 중심 (1 샘플)
    {
        bool isCorrectLayer = (IsFarField == 0)
            ? (centerCocSigned < -COC_THRESHOLD)
            : (centerCocSigned > COC_THRESHOLD);

        if (isCorrectLayer)
        {
            accColor += centerColor;
            accWeight += 1.0;
            validSampleCount += 1.0;
        }
    }

    // Ring 1~MAX_RING: 원형 샘플링
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
            float sampleCocSigned = sampleVal.a;
            float sampleCoc = abs(sampleCocSigned);

            // Layer Processing: 해당 레이어만 포함
            bool isCorrectLayer = (IsFarField == 0)
                ? (sampleCocSigned < -COC_THRESHOLD)
                : (sampleCocSigned > COC_THRESHOLD);

            if (!isCorrectLayer)
            {
                continue;
            }

            // 단순 CoC 기반 가중치
            float weight = saturate(sampleCoc);
            accColor += sampleColor * weight;
            accWeight += weight;
            validSampleCount += 1.0;
        }
    }

    // 6. 출력
    // 해당 레이어 샘플이 없으면 투명 출력
    if (accWeight < 0.0001)
    {
        output.Color = float4(0, 0, 0, 0);
        return output;
    }

    // RGB: 가중 평균
    float3 avgColor = accColor / accWeight;

    // Alpha: 유효 샘플 수 기반 (정규화)
    // 샘플이 충분하면 불투명, 부족하면 반투명 (블리딩)
    float finalAlpha = saturate(validSampleCount / 32.0);

    // Premultiplied Alpha 변환
    output.Color = float4(avgColor * finalAlpha, finalAlpha);

    return output;
}
