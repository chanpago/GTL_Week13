

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

// b3: Particle Required Parameters
cbuffer ParticleBuffer : register(b3)
{
    uint ScreenAlignment;   // 0: PSA_Square, 1: PSA_Velocity
    float3 Padding;         // 16바이트 정렬용 패딩
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
    float3 Velocity : TEXCOORD4;
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

    // 카메라 정보를 ViewMatrix에서 추출
    float3 CamRight = InverseViewMatrix[0].xyz;
    float3 CamUp    = InverseViewMatrix[1].xyz;
    float3 CamPos   = InverseViewMatrix[3].xyz;
    
    float3 FinalRight;
    float3 FinalUp;
    float2 finalSize = In.Size;
    
    if (ScreenAlignment == 1) // [PSA_Velocity]
    {
        // 속도가 거의 0일 때는 깨짐 방지를 위해 카메라 정면 로직 사용
        float SpeedSq = dot(In.Velocity, In.Velocity);
        if (SpeedSq < 0.001f)
        {
            FinalRight = CamRight;
            FinalUp    = CamUp;
        }
        else
        {
            float Speed = sqrt(SpeedSq);
            // Y축을 속도 방향으로 강제 고정
            FinalUp = normalize(In.Velocity);

            // X축은 속도와 카메라 방향의 외적으로 구함
            float3 ToCam = normalize(CamPos - In.Position);
            FinalRight = normalize(cross(ToCam, FinalUp));
            finalSize.y += (Speed * 0.2f);
        }
        // Velocity 모드에서 회전 무시
    }
    else // [PSA_Square]
    {
        float cosR = cos(In.Rotation);
        float sinR = sin(In.Rotation);

        // 회전 행렬 적용
        FinalRight = CamRight * cosR + CamUp * sinR;
        FinalUp    = CamUp * cosR - CamRight * sinR; 
    }
    float2 halfSize = finalSize * 0.5f;
    float3 worldPos = In.Position 
                    + (FinalRight * In.Corner.x * halfSize.x) 
                    + (FinalUp    * In.Corner.y * halfSize.y);

    float4 viewPos = mul(float4(worldPos, 1.0f), ViewMatrix);
    Out.Position   = mul(viewPos, ProjectionMatrix);

    // DirectX는 V가 위에서 아래로 증가하므로 Y(V)를 반전
    Out.UV = float2(In.Corner.x * 0.5f + 0.5f, -In.Corner.y * 0.5f + 0.5f);
    Out.Color = In.Color;
    Out.SubImageIndex = In.SubImageIndex;

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
            float4 finalColor = tex * In.Color;

            // 알파가 0에 가까우면 discard
            // 근데 굳이 해야할 필요 못느끼겠음 어차피 사라지는데 이게 뭐하는짓임?
            if (finalColor.a < 0.01)
            {
                discard;
            }
            
            return finalColor;
        }
    }

    // 일반 샘플링 (SubUV 없거나 None 방식)
    float4 tex = ParticleTex.Sample(ParticleSampler, uv);
    float4 color = tex * In.Color;

    // 알파 또는 검은색에 가까우면 discard
    if (color.a < 0.01 || (color.r < 0.005 && color.g < 0.005 && color.b < 0.005))
    {
        discard;
    }
    
    return color;
}