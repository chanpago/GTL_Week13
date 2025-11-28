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
float CalculateCoC(
    float viewDepth,             // View-space depth (m 단위)
    float focalDistance,         // 초점 거리 (m)
    float fstop,                 // 조리개 값 (F-stop)
    float sensorWidth,           // 센서 너비 (m, e.g., 0.024 = 24mm)
    float focalRegion,           // 완전 선명 영역 (m)
    float nearTransition,        // 근경 전환 영역 (m)
    float farTransition,         // 원경 전환 영역 (m)
    float maxNearBlurSize,       // 근경 최대 블러 (정규화, 0~1)
    float maxFarBlurSize         // 원경 최대 블러 (정규화, 0~1)
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

    // 3. CoC 계산
    float coc = 0.0;

    if (viewDepth < focalStart)
    {
        // 근경 (Near Field) - 음수 CoC
        float distance = focalStart - viewDepth;
        float normalizedCoC = saturate(distance / nearTransition);
        coc = -normalizedCoC * maxNearBlurSize;
    }
    else  // viewDepth > focalEnd
    {
        // 원경 (Far Field) - 양수 CoC
        float distance = viewDepth - focalEnd;
        float normalizedCoC = saturate(distance / farTransition);
        coc = normalizedCoC * maxFarBlurSize;
    }

    return coc;
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

// 4x4 다운샘플 (Depth-Aware)
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
