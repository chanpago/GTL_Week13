// b0: ModelBuffer (VS) - Matches ModelBufferType exactly (128 bytes)
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix; // 64 bytes
    row_major float4x4 WorldInverseTranspose; // 64 bytes - For correct normal transformation
};

// b1: ViewProjBuffer (VS) - Matches ViewProjBufferType
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
};

// b2: SubUV Parameters
cbuffer SubUVBuffer : register(b2)
{
    int SubImages_Horizontal;  // NX
    int SubImages_Vertical;    // NY
    int InterpMethod;          // 0=None, 1=LinearBlend
    float Padding0;            // 16바이트 정렬
};

Texture2D ParticleTex : register(t0);
SamplerState ParticleSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float2 Corner : TEXCOORD0;
    float2 Size : TEXCOORD1;
    float4 Color : COLOR0;
    float Rotation : TEXCOORD2;
    float SubImageIndex : TEXCOORD3;  // SubUV 애니메이션용 float 프레임 인덱스
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
    float SubImageIndex : TEXCOORD1;  // VS에서 PS로 전달
};

PSInput mainVS(VSInput In)
{
    PSInput Out;

    // 카메라 오른쪽/위 벡터를 ViewMatrix에서 추출
    // float3 Right = InverseViewMatrix[0].xyz;
    // float3 Up = InverseViewMatrix[1].xyz;
    float3 Right = normalize(float3(ViewMatrix._11, ViewMatrix._21, ViewMatrix._31));
    float3 Up = normalize(float3(ViewMatrix._12, ViewMatrix._22, ViewMatrix._32));

    float2 halfSize = In.Size * 0.5f;

    // Rotation 적용: Corner를 회전시킴
    float cosR = cos(In.Rotation);
    float sinR = sin(In.Rotation);
    float2 rotatedCorner;
    rotatedCorner.x = In.Corner.x * cosR - In.Corner.y * sinR;
    rotatedCorner.y = In.Corner.x * sinR + In.Corner.y * cosR;

    float3 worldPos =
        In.Position
        + Right * rotatedCorner.x * halfSize.x
        + Up * rotatedCorner.y * halfSize.y;

    float4 viewPos = mul(float4(worldPos, 1.0f), ViewMatrix);
    float4 projPos = mul(viewPos, ProjectionMatrix);

    Out.Position = projPos;
    Out.UV = In.Corner * 0.5f + 0.5f; // (-1~1) → (0~1)
    Out.Color = In.Color;
    Out.SubImageIndex = In.SubImageIndex;  // PS로 전달

    return Out;
}

float4 mainPS(PSInput In) : SV_TARGET
{
    float2 uv = In.UV;

    // SubUV 활성화 체크 (NX * NY > 1이면 SubUV 사용)
    int TotalFrames = SubImages_Horizontal * SubImages_Vertical;
    if (TotalFrames > 1)
    {
        // float Index를 클램프
        float I = clamp(In.SubImageIndex, 0.0, float(TotalFrames - 1));

        // 보간 없음 (None)
        if (InterpMethod == 0)
        {
            int frame = int(floor(I));
            int tileX = frame % SubImages_Horizontal;
            int tileY = frame / SubImages_Horizontal;

            float2 scale = float2(1.0 / SubImages_Horizontal, 1.0 / SubImages_Vertical);
            float2 offset = float2(tileX, tileY) * scale;

            uv = In.UV * scale + offset;
        }
        // LinearBlend (두 프레임 보간)
        else if (InterpMethod == 1)
        {
            int frame0 = int(floor(I));
            int frame1 = min(frame0 + 1, TotalFrames - 1);
            float alpha = frac(I);

            // Frame 0 UV 계산
            int tileX0 = frame0 % SubImages_Horizontal;
            int tileY0 = frame0 / SubImages_Horizontal;
            float2 scale = float2(1.0 / SubImages_Horizontal, 1.0 / SubImages_Vertical);
            float2 offset0 = float2(tileX0, tileY0) * scale;
            float2 uv0 = In.UV * scale + offset0;

            // Frame 1 UV 계산
            int tileX1 = frame1 % SubImages_Horizontal;
            int tileY1 = frame1 / SubImages_Horizontal;
            float2 offset1 = float2(tileX1, tileY1) * scale;
            float2 uv1 = In.UV * scale + offset1;

            // 두 프레임 샘플 후 보간
            float4 c0 = ParticleTex.Sample(ParticleSampler, uv0);
            float4 c1 = ParticleTex.Sample(ParticleSampler, uv1);
            float4 tex = lerp(c0, c1, alpha);

            return tex * In.Color;
        }
    }

    // 일반 샘플링 (SubUV 없거나 None 방식)
    float4 tex = ParticleTex.Sample(ParticleSampler, uv);
    float4 color = tex * In.Color;

    return color;
}
