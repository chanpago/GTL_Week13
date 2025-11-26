//--------------------------------------------------------------------------------------
// FXAA Pixel Shader (NVIDIA FXAA 3.11 style - Diagonal sampling)
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// 상수 버퍼 (Constant Buffer)
//--------------------------------------------------------------------------------------
cbuffer FXAAParametersCB : register(b2)
{
    float2 InvScreenSize;   // 1.0f / ScreenSize (픽셀 하나의 크기)
    float SpanMax;          // 최대 탐색 범위 (8.0f 권장)
    float ReduceMul;        // 감쇠 승수 (1/8 = 0.125f 권장)

    float ReduceMin;        // 최소 감쇠 값 (1/128 = 0.0078125f 권장)
    float SubPixBlend;      // 서브픽셀 블렌딩 강도 (0.75~1.0 권장)
    float2 Padding;
};

//--------------------------------------------------------------------------------------
// 텍스처와 샘플러
//--------------------------------------------------------------------------------------
Texture2D g_SceneTexture : register(t0);
SamplerState g_SamplerLinear : register(s0);

//--------------------------------------------------------------------------------------
// RGB 색상에서 휘도(Luminance)를 계산하는 함수
//--------------------------------------------------------------------------------------
float Luma(float3 color)
{
    // ITU-R BT.601 기반 휘도 공식 (FXAA에서 일반적으로 사용)
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

//--------------------------------------------------------------------------------------
// FXAA Pixel Shader
//--------------------------------------------------------------------------------------
float4 mainPS(float4 Pos : SV_Position, float2 TexCoord : TEXCOORD0) : SV_Target
{
    float2 inv = InvScreenSize;

    // 1. 대각선 4개 + 중심 픽셀 샘플링 (FXAA 3.11 방식)
    float3 rgbNW = g_SceneTexture.Sample(g_SamplerLinear, TexCoord + float2(-inv.x, -inv.y)).rgb;
    float3 rgbNE = g_SceneTexture.Sample(g_SamplerLinear, TexCoord + float2( inv.x, -inv.y)).rgb;
    float3 rgbSW = g_SceneTexture.Sample(g_SamplerLinear, TexCoord + float2(-inv.x,  inv.y)).rgb;
    float3 rgbSE = g_SceneTexture.Sample(g_SamplerLinear, TexCoord + float2( inv.x,  inv.y)).rgb;
    float3 rgbM  = g_SceneTexture.Sample(g_SamplerLinear, TexCoord).rgb;

    // 2. 휘도 계산
    float lumaNW = Luma(rgbNW);
    float lumaNE = Luma(rgbNE);
    float lumaSW = Luma(rgbSW);
    float lumaSE = Luma(rgbSE);
    float lumaM  = Luma(rgbM);

    // 3. 지역 휘도 범위 계산
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float lumaRange = lumaMax - lumaMin;

    // 4. 엣지 감지 (조기 종료) - 대비가 낮으면 원본 반환
    float threshold = max(0.0625f, lumaMax * 0.125f);
    if (lumaRange < threshold)
    {
        return float4(rgbM, 1.0f);
    }

    // 5. 엣지 방향 계산
    // dir.x: 수직 방향 그라디언트 (위 vs 아래)
    // dir.y: 수평 방향 그라디언트 (왼쪽 vs 오른쪽)
    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    // 6. 방향 벡터 정규화 및 감쇠
    // 노이즈/저대비 영역에서 방향이 과민하게 커지는 것을 방지
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * ReduceMul), ReduceMin);
    float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    // 7. 방향 스케일링 및 클램핑
    float2 dirScaled = dir * rcpDirMin;
    dirScaled = clamp(dirScaled, -SpanMax, SpanMax);
    dir = dirScaled * inv;

    // 8. 엣지 방향을 따라 샘플링 (2-tap)
    float3 rgbA = 0.5f * (
        g_SceneTexture.Sample(g_SamplerLinear, TexCoord + dir * (1.0f / 3.0f - 0.5f)).rgb +
        g_SceneTexture.Sample(g_SamplerLinear, TexCoord + dir * (2.0f / 3.0f - 0.5f)).rgb
    );

    // 9. 더 넓은 범위 샘플링 (4-tap blend)
    float3 rgbB = rgbA * 0.5f + 0.25f * (
        g_SceneTexture.Sample(g_SamplerLinear, TexCoord + dir * -0.5f).rgb +
        g_SceneTexture.Sample(g_SamplerLinear, TexCoord + dir *  0.5f).rgb
    );

    // 10. 가드 밴드 체크 - rgbB가 범위를 벗어나면 rgbA 사용
    float lumaB = Luma(rgbB);
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    {
        rgbB = rgbA;
    }

    // 11. 서브픽셀 블렌딩
    float3 finalColor = lerp(rgbM, rgbB, SubPixBlend);

    return float4(finalColor, 1.0f);
}
