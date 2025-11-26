// ParticleRibbon.hlsl

cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
    row_major float4x4 WorldInverseTranspose;
};

cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
};

cbuffer SubUVBuffer : register(b2)
{
    int SubImages_Horizontal; // NX
    int SubImages_Vertical; // NY
    int InterpMethod; // 0 = None, 1 = Linear
    float Padding0;
};

Texture2D RibbonTex : register(t0);
SamplerState RibbonSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION; // world space position
    float2 UV : TEXCOORD0; // Corner에 써둔 (U,V)
    float2 Size : TEXCOORD1; // 사용 안 함
    float4 Color : COLOR0;
    float Rotation : TEXCOORD2; // 사용 안 함
    float SubImageIndex : TEXCOORD3;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
    float SubImageIndex : TEXCOORD1;
};

PSInput mainVS(VSInput In)
{
    PSInput Out;

    float4 WorldPos = float4(In.Position, 1.0f);
    float4 ViewPos = mul(WorldPos, ViewMatrix);
    Out.Position = mul(ViewPos, ProjectionMatrix);

    Out.UV = In.UV;
    Out.Color = In.Color;
    Out.SubImageIndex = In.SubImageIndex;
    return Out;
}

float4 mainPS(PSInput In) : SV_TARGET
{
    float2 uv = In.UV;
    int TotalFrames = SubImages_Horizontal * SubImages_Vertical;

    if (TotalFrames > 1)
    {
        float I = clamp(In.SubImageIndex, 0.0, float(TotalFrames - 1));

        if (InterpMethod == 0)
        {
            int frame = int(floor(I));
            int tileX = frame % SubImages_Horizontal;
            int tileY = frame / SubImages_Horizontal;

            float2 scale = float2(1.0 / SubImages_Horizontal, 1.0 / SubImages_Vertical);
            float2 offset = float2(tileX, tileY) * scale;

            uv = uv * scale + offset;
        }
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
            float4 c0 = RibbonTex.Sample(RibbonSampler, uv0);
            float4 c1 = RibbonTex.Sample(RibbonSampler, uv1);
            float4 tex = lerp(c0, c1, alpha);
            float4 finalColor = tex * In.Color;
            
            return finalColor;
        }
    }

    float4 tex = RibbonTex.Sample(RibbonSampler, uv);
    if (length(tex.rgb) < 0.001f && tex.a < 0.001f)
    {
        tex = float4(1.0f, 1.0f, 0.0f, 1.0f); // 기본 노란색
    }
    
    float4 col = tex; // * In.Color;
    return col;
}
