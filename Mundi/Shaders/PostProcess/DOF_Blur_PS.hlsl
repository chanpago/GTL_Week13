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

// Interleaved Gradient Noise - 픽셀당 고유한 노이즈 생성
// Jorge Jimenez의 방식: 시간적 안정성이 좋고 계산 비용이 낮음
float InterleavedGradientNoise(float2 pixelPos)
{
    return frac(52.9829189 * frac(0.06711056 * pixelPos.x + 0.00583715 * pixelPos.y));
}

// 알파 스케일 팩터: normalizedCoverage를 0~1 범위로 조정
// - 샘플의 일부만 해당 레이어에 속함 (layer filtering)
// - coverage 값이 평균적으로 1.0보다 작음 (distance falloff, depth weight)
// - 경험적으로 4.0이 대부분의 경우 적절한 블러 불투명도를 생성
static const float ALPHA_SCALE_FACTOR = 4.0;

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

    // 2. Tile에서 Max CoC 가져오기 (Point Sampling)
    // tileData: x=MaxCoC (Near/Far 통합), y=MinCoC, z,w=unused
    float2 pixelPos = input.texCoord * ScreenSize.xy;
    float2 tileCount = ScreenSize.xy / TILE_SIZE;
    int2 tileIdx = int2(pixelPos / TILE_SIZE);
    tileIdx = clamp(tileIdx, int2(0, 0), int2(tileCount) - 1);
    float4 tileData = g_TileTex.Load(int3(tileIdx, 0));
    float tileMaxCoc = tileData.x;  // Dilated Max CoC (절대값)

    // In-Focus 임계값 (이 이하는 선명 영역)
    const float COC_THRESHOLD = 0.01;

    // 4. 검색 반경 결정
    // CoC는 0~1 정규화, BlurRadius는 Full Res 픽셀
    float halfBlurRadius = BlurRadius * 0.5;  // Half Res로 변환

    // Edge bleeding: tileMaxCoc가 있으면 약간 더 넓게 검색 (2.0x)
    float searchRadius = max(centerCoc, tileMaxCoc * 2.0);  // 0~1 정규화
    float pixelRadius = searchRadius * halfBlurRadius;  // Half Res 픽셀
    pixelRadius = clamp(pixelRadius, 1.0, halfBlurRadius);

    // 5. Ring-based 2D Gather
    float2 texelSize = ScreenSize.zw;
    float3 accColor = float3(0, 0, 0);
    float accWeight = 0.0;
    float validSampleCount = 0.0;

    // Ring 설정
    const int MAX_RING = 7;  // Ring 0(중심) + Ring 1~7 = 253 샘플

    // ========== Jittering: 픽셀당 랜덤 회전 ==========
    // 규칙적인 패턴(Artifact)을 불규칙한 노이즈로 변환
    // 큰 CoC에서 샘플 간격이 벌어져 생기는 띠 현상을 제거
    float randomRotation = InterleavedGradientNoise(pixelPos) * 2.0 * PI;

    // Ring 0: 중심 (1 샘플)
    {
        // Layer Processing: 해당 레이어만 포함 (In-Focus 제외!)
        bool isCorrectLayer = (IsFarField == 0)
            ? (centerCocSigned < -COC_THRESHOLD)
            : (centerCocSigned > COC_THRESHOLD);

        if (isCorrectLayer)
        {
            // 균등 가중치
            float weight = 1.0;
            accColor += centerColor * weight;
            accWeight += weight;
            validSampleCount += 1.0;
        }
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
            // Jittering: 랜덤 회전 각도 추가로 샘플링 패턴을 픽셀마다 다르게
            float angle = float(s) * angleStep + randomRotation;
            float2 offset = float2(cos(angle), sin(angle)) * ringRadius * texelSize;
            float2 sampleUV = input.texCoord + offset;

            float4 sampleVal = g_InputTex.Sample(g_LinearClampSample, sampleUV);
            float3 sampleColor = sampleVal.rgb;
            float sampleCocSigned = sampleVal.a;  // 부호 있음
            float sampleCoc = abs(sampleCocSigned);  // 블러 크기는 절대값

            // Scatter-as-Gather: 샘플의 CoC가 현재 위치까지 도달하는지 검사
            float sampleRadiusPx = sampleCoc * halfBlurRadius;

            // 샘플의 블러 반경이 현재 거리까지 도달하는 정도
            float coverageRange = max(sampleRadiusPx, 2.0);
            float coverage = saturate((coverageRange - ringRadius + 2.0) / coverageRange);

            // 거리 기반 감쇠
            float sigma = pixelRadius * 0.7;  // 0.5(너무 강함) ~ 1.0(너무 약함) 사이
            float distanceFalloff = exp(-(ringRadius * ringRadius) / (2.0 * sigma * sigma));

            // Layer Processing: 해당 레이어만 포함 (In-Focus 제외!)
            // Near pass (IsFarField=0): CoC < -threshold (실제 Near만)
            // Far pass (IsFarField=1): CoC > threshold (실제 Far만, In-Focus 제외)
            bool isCorrectLayer = (IsFarField == 0)
                ? (sampleCocSigned < -COC_THRESHOLD)
                : (sampleCocSigned > COC_THRESHOLD);

            if (!isCorrectLayer)
            {
                continue;  // In-Focus나 다른 레이어 샘플은 무시
            }

            // ========== 깊이 비교 (Color Leaking 방지) ==========
            float depthWeight = 1.0;
            if (IsFarField == 1)
            {
                // Far 패스: 샘플이 나보다 앞에 있으면 가중치 감소
                float depthDiff = centerCocSigned - sampleCocSigned;
                depthWeight = saturate(1.0 - depthDiff * 0.5);
            }
            else
            {
                // Near 패스: 샘플이 나보다 뒤에 있으면 가중치 감소
                float depthDiff = sampleCocSigned - centerCocSigned;
                depthWeight = saturate(1.0 - depthDiff * 0.5);
            }

            // 가중치 = Coverage * 거리감쇠 * DepthWeight (Area weight 제거)
            float weight = coverage * distanceFalloff * depthWeight;
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
