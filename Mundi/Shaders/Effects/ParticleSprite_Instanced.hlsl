// GPU instanced particle sprite shader.
// Matches ParticleSprite.hlsl behaviour but reads per-instance data from a structured buffer
// so that CPU uploads only one record per sprite.

// b0: ModelBuffer (kept for interface parity ? values unused for billboards)
cbuffer ModelBuffer : register(b0)
{
	row_major float4x4 WorldMatrix;
	row_major float4x4 WorldInverseTranspose;
};

// b1: View/Projection like the non-instanced version
cbuffer ViewProjBuffer : register(b1)
{
	row_major float4x4 ViewMatrix;
	row_major float4x4 ProjectionMatrix;
	row_major float4x4 InverseViewMatrix;
	row_major float4x4 InverseProjectionMatrix;
};

Texture2D ParticleTex : register(t0);
SamplerState ParticleSampler : register(s0);

// Structured buffer carrying FParticleInstanceData written on the CPU side.
struct FParticleInstanceData
{
	float3 Position;
	float2 Size;
	float4 Color;
	float Rotation;
};

StructuredBuffer<FParticleInstanceData> ParticleInstances : register(t14);

struct VSInput
{
	uint VertexID : SV_VertexID;
	uint InstanceID : SV_InstanceID;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
};

static const uint CornerIndexLUT[6] =
{
    0, 1, 2,
	2, 3, 0
};

static const float2 CornerOffsets[4] = {
	float2(-1.0f, -1.0f), float2(1.0f, -1.0f),
	float2(1.0f,  1.0f),  float2(-1.0f,  1.0f)
};

VSOutput mainVS(VSInput In)
{
	VSOutput Out;
	FParticleInstanceData Instance = ParticleInstances[In.InstanceID];

    uint cornerIndex = CornerIndexLUT[In.VertexID];
    float2 corner = CornerOffsets[cornerIndex];
	float2 halfSize = Instance.Size * 0.5f;

	float cosR = cos(Instance.Rotation);
	float sinR = sin(Instance.Rotation);
	float2 rotatedCorner;
	rotatedCorner.x = corner.x * cosR - corner.y * sinR;
	rotatedCorner.y = corner.x * sinR + corner.y * cosR;

	float3 Right = normalize(float3(ViewMatrix._11, ViewMatrix._21, ViewMatrix._31));
	float3 Up = normalize(float3(ViewMatrix._12, ViewMatrix._22, ViewMatrix._32));

	float3 worldPos = Instance.Position
		+ Right * rotatedCorner.x * halfSize.x
		+ Up * rotatedCorner.y * halfSize.y;

	float4 viewPos = mul(float4(worldPos, 1.0f), ViewMatrix);
	Out.Position = mul(viewPos, ProjectionMatrix);
	Out.UV = corner * 0.5f + 0.5f;
	Out.Color = Instance.Color;
	return Out;
}

float4 mainPS(VSOutput In) : SV_TARGET
{
	float4 tex = ParticleTex.Sample(ParticleSampler, In.UV);
	return tex * In.Color;
}
