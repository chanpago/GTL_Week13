// DOF (Depth of Field) 공유 함수
// Setup, Blur, Recombine Pass에서 공통으로 사용

#ifndef DOF_COMMON_HLSLI
#define DOF_COMMON_HLSLI

// Depth를 Linear View-Space Depth로 변환
float LinearizeDepth(float rawDepth, float nearPlane, float farPlane, int isOrthographic)
{
    if (isOrthographic == 1)
    {
        // Orthographic: 선형 보간
        return rawDepth * (farPlane - nearPlane) + nearPlane;
    }
    else
    {
        // Perspective: NDC -> View Space
        float z_n = 2.0 * rawDepth - 1.0;  // [0,1] -> [-1,1]
        return 2.0 * nearPlane * farPlane / (farPlane + nearPlane - z_n * (farPlane - nearPlane));
    }
}

// Circle of Confusion (CoC) 계산
// 반환값: 양수 = Far Blur (원경), 음수 = Near Blur (근경), 0 = 선명
// **0~1 정규화된 값 반환** (픽셀 단위 아님)
float CalculateCoC(
    float viewDepth,             // View-space depth (m 단위)
    float focalDistance,         // 초점 거리 (m)
    float focalRegion,           // 완전 선명 영역 (m)
    float nearTransition,        // 근경 전환 영역 (m)
    float farTransition,         // 원경 전환 영역 (m)
    float maxNearBlurSize,       // 근경 최대 블러 (pixels, 미사용)
    float maxFarBlurSize         // 원경 최대 블러 (pixels, 미사용)
)
{
    // 1. 초점 영역 계산
    float focalStart = focalDistance - focalRegion * 0.5;
    float focalEnd = focalDistance + focalRegion * 0.5;

    // 2. 초점 영역 내부면 선명
    if (viewDepth >= focalStart && viewDepth <= focalEnd)
    {
        return 0.0;
    }

    // 3. CoC 계산 (0~1 정규화)
    float coc = 0.0;

    if (viewDepth < focalStart)
    {
        // 근경 (Near Field) - 음수 CoC
        float distance = focalStart - viewDepth;
        float normalizedCoC = saturate(distance / nearTransition);
        coc = -normalizedCoC;  // 0~1 정규화
    }
    else  // viewDepth > focalEnd
    {
        // 원경 (Far Field) - 양수 CoC
        float distance = viewDepth - focalEnd;
        float normalizedCoC = saturate(distance / farTransition);
        coc = normalizedCoC;  // 0~1 정규화
    }

    return coc;
}

// Circle of Confusion (CoC) 계산 - Far Clip 체크 포함
// farClip 근처 깊이는 하늘/무한대로 간주하고 CoC = 0 반환
float CalculateCoCWithSkyCheck(
    float viewDepth,
    float focalDistance,
    float focalRegion,
    float nearTransition,
    float farTransition,
    float maxNearBlurSize,
    float maxFarBlurSize,
    float farClip
)
{
    // 하늘/배경 체크: Far Clip의 95% 이상이면 무한대로 간주 → 블러 안함
    if (viewDepth > farClip * 0.99)
    {
        return 0.0;
    }

    return CalculateCoC(
        viewDepth,
        focalDistance,
        focalRegion,
        nearTransition,
        farTransition,
        maxNearBlurSize,
        maxFarBlurSize
    );
}

// 간단한 CoC 계산 (물리 기반 생략, 성능 우선)
float CalculateCoCSimple(
    float viewDepthCm,
    float focalDistanceCm,
    float focalRegionCm,
    float transitionRegionCm,
    float maxBlurSize
)
{
    float focalStart = focalDistanceCm - focalRegionCm * 0.5;
    float focalEnd = focalDistanceCm + focalRegionCm * 0.5;

    if (viewDepthCm >= focalStart && viewDepthCm <= focalEnd)
    {
        return 0.0;
    }

    float distance = (viewDepthCm < focalStart)
        ? (focalStart - viewDepthCm)
        : (viewDepthCm - focalEnd);

    float normalizedCoC = saturate(distance / transitionRegionCm);
    float sign = (viewDepthCm < focalStart) ? -1.0 : 1.0;

    return normalizedCoC * maxBlurSize * sign;
}

// 4x4 다운샘플 (단순 박스 필터)
float4 DownsampleSceneColor4x4(Texture2D sceneTex, SamplerState linearSampler, float2 uv, float2 texelSize)
{
    // 2x2 박스 필터로 4개 샘플 평균
    float4 color = float4(0, 0, 0, 0);

    color += sceneTex.Sample(linearSampler, uv + float2(-0.5, -0.5) * texelSize);
    color += sceneTex.Sample(linearSampler, uv + float2( 0.5, -0.5) * texelSize);
    color += sceneTex.Sample(linearSampler, uv + float2(-0.5,  0.5) * texelSize);
    color += sceneTex.Sample(linearSampler, uv + float2( 0.5,  0.5) * texelSize);

    return color * 0.25;
}

// Bilateral 다운샘플 (깊이 기반 가중치로 엣지 보존)
// depthSigma: 깊이 차이에 대한 감도 (낮을수록 엣지 보존 강함)
float4 BilateralDownsample4x4(
    Texture2D sceneTex,
    Texture2D depthTex,
    SamplerState linearSampler,
    SamplerState pointSampler,
    float2 uv,
    float2 texelSize,
    float nearPlane,
    float farPlane,
    int isOrthographic,
    float depthSigma
)
{
    // 샘플 오프셋 (2x2 패턴)
    static const float2 offsets[4] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    // 중심 깊이 (기준점)
    float centerRawDepth = depthTex.Sample(pointSampler, uv).r;
    float centerLinearDepth = LinearizeDepth(centerRawDepth, nearPlane, farPlane, isOrthographic);

    float4 colorSum = float4(0, 0, 0, 0);
    float weightSum = 0.0;

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + offsets[i] * texelSize;

        // 색상 샘플
        float4 sampleColor = sceneTex.Sample(linearSampler, sampleUV);

        // 깊이 샘플 및 선형화
        float sampleRawDepth = depthTex.Sample(pointSampler, sampleUV).r;
        float sampleLinearDepth = LinearizeDepth(sampleRawDepth, nearPlane, farPlane, isOrthographic);

        // 깊이 차이 기반 가중치 (Bilateral Weight)
        float depthDiff = abs(sampleLinearDepth - centerLinearDepth);
        float depthWeight = exp(-depthDiff * depthDiff / (2.0 * depthSigma * depthSigma));

        colorSum += sampleColor * depthWeight;
        weightSum += depthWeight;
    }

    // 가중 평균 (0으로 나누기 방지)
    return colorSum / max(weightSum, 0.0001);
}

// Bilateral 다운샘플 + CoC 반환 버전
// 색상과 함께 중심 CoC도 계산하여 반환
struct BilateralDownsampleResult
{
    float4 color;
    float coc;
    float linearDepth;
};

BilateralDownsampleResult BilateralDownsampleWithCoC(
    Texture2D sceneTex,
    Texture2D depthTex,
    SamplerState linearSampler,
    SamplerState pointSampler,
    float2 uv,
    float2 texelSize,
    float nearPlane,
    float farPlane,
    int isOrthographic,
    float depthSigma,
    float focalDistance,
    float focalRegion,
    float nearTransition,
    float farTransition,
    float maxNearBlur,
    float maxFarBlur
)
{
    BilateralDownsampleResult result;

    // 샘플 오프셋 (2x2 패턴)
    static const float2 offsets[4] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    // 중심 깊이
    float centerRawDepth = depthTex.Sample(pointSampler, uv).r;
    float centerLinearDepth = LinearizeDepth(centerRawDepth, nearPlane, farPlane, isOrthographic);

    float4 colorSum = float4(0, 0, 0, 0);
    float weightSum = 0.0;
    float cocSum = 0.0;

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + offsets[i] * texelSize;

        // 색상 샘플
        float4 sampleColor = sceneTex.Sample(linearSampler, sampleUV);

        // 깊이 샘플 및 선형화
        float sampleRawDepth = depthTex.Sample(pointSampler, sampleUV).r;
        float sampleLinearDepth = LinearizeDepth(sampleRawDepth, nearPlane, farPlane, isOrthographic);

        // 각 샘플의 CoC 계산
        float sampleCoC = CalculateCoC(
            sampleLinearDepth,
            focalDistance,
            focalRegion,
            nearTransition,
            farTransition,
            maxNearBlur,
            maxFarBlur
        );

        // 깊이 차이 기반 가중치
        float depthDiff = abs(sampleLinearDepth - centerLinearDepth);
        float depthWeight = exp(-depthDiff * depthDiff / (2.0 * depthSigma * depthSigma));

        colorSum += sampleColor * depthWeight;
        cocSum += sampleCoC * depthWeight;
        weightSum += depthWeight;
    }

    // 가중 평균
    float invWeight = 1.0 / max(weightSum, 0.0001);
    result.color = colorSum * invWeight;
    result.coc = cocSum * invWeight;
    result.linearDepth = centerLinearDepth;

    return result;
}

// ==========================================
// Scatter 대상 선별 (Setup과 Scatter에서 공유)
// ==========================================

// Luminance 계산
float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Scatter 조건 상수
static const float SCATTER_LUMA_THRESHOLD = 0.4;   // 밝기 임계값
static const float SCATTER_FACTOR_THRESHOLD = 0.5; // scatterFactor 임계값
static const float SCATTER_COC_THRESHOLD = 0.1;    // CoC 임계값

// Scatter 대상 여부 판단
// 반환: true = Scatter로 처리, false = Gather로 처리
bool ShouldScatter(float3 color, float coc)
{
    float luma = Luminance(color);
    float scatterFactor = saturate(luma / SCATTER_LUMA_THRESHOLD - 1.0);
    return (scatterFactor > SCATTER_FACTOR_THRESHOLD) && (abs(coc) > SCATTER_COC_THRESHOLD);
}

// Gather용 가중치 계산 (Scatter 대상이 아닌 경우)
float CalculateGatherWeight(float3 color, float coc)
{
    float luma = Luminance(color);
    float distinction = abs(coc) * luma;
    return smoothstep(100.0, -100.0, distinction);
}

// ==========================================

// Gaussian 가중치 (9-tap)
static const float GAUSSIAN_WEIGHTS_9[5] =
{
    0.2270270270,  // center
    0.1945945946,  // ±1
    0.1216216216,  // ±2
    0.0540540541,  // ±3
    0.0162162162   // ±4
};

// Gaussian 가중치 (13-tap)
static const float GAUSSIAN_WEIGHTS_13[7] =
{
    0.1964825502,  // center
    0.1763662950,  // ±1
    0.1219842630,  // ±2
    0.0648996386,  // ±3
    0.0263931800,  // ±4
    0.0081430332,  // ±5
    0.0019131875   // ±6
};

#endif // DOF_COMMON_HLSLI
