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

struct VSInput
{
    float3 Position : POSITION;
    float2 Corner : TEXCOORD0;
    float2 Size : TEXCOORD1;
    float4 Color : COLOR0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

PSInput mainVS(VSInput In)
{
    PSInput Out;

    // 카메라 오른쪽/위 벡터를 ViewMatrix에서 추출
    float3 Right = InverseViewMatrix[0].xyz;
    float3 Up = InverseViewMatrix[1].xyz;

    float2 halfSize = In.Size * 0.5f;

    float3 worldPos =
        In.Position
        + Right * In.Corner.x * halfSize.x
        + Up * In.Corner.y * halfSize.y;

    float4 viewPos = mul(float4(worldPos, 1.0f), ViewMatrix);
    float4 projPos = mul(viewPos, ProjectionMatrix);

    Out.Position = projPos;
    Out.UV = In.Corner * 0.5f + 0.5f; // (-1~1) → (0~1)
    Out.Color = In.Color;

    return Out;
}

float4 mainPS(PSInput In) : SV_TARGET
{
    return In.Color;
}
