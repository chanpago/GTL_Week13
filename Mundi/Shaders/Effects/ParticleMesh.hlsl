//================================================================================================
// Filename:      ParticleMesh.hlsl
// Description:   Mesh Particle 전용 셰이더
//                - 기본적으로 UberLit 기반이지만 파티클 특화 기능 지원
//                - 조명 on/off 가능 (PARTICLE_LIGHTING 매크로)
//================================================================================================

// 조명 모델 선택
// #define LIGHTING_MODEL_PHONG 1

#ifndef PARTICLE_LIGHTING
#define PARTICLE_LIGHTING 0  // 기본값: 조명 비적용
#endif

// Material 구조체
struct FMaterial
{
    float3 DiffuseColor;
    float OpticalDensity;
    float3 AmbientColor;
    float Transparency;
    float3 SpecularColor;
    float SpecularExponent;
    float3 EmissiveColor;
    uint IlluminationModel;
    float3 TransmissionFilter;
    float Padding;
};

// 상수 버퍼
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

cbuffer ColorBuffer : register(b3)
{
    float4 LerpColor;
    uint UUID;
};

cbuffer PixelConstBuffer : register(b4)
{
    FMaterial Material;
    uint bHasMaterial;
    uint bHasTexture;
    uint bHasNormalTexture;
};

#define SPECULAR_COLOR (bHasMaterial ? Material.SpecularColor : float3(1.0f, 1.0f, 1.0f))

// 텍스처 및 샘플러
Texture2D g_DiffuseTexColor : register(t0);
Texture2D g_NormalTexColor : register(t1);
Texture2D g_ShadowAtlas2D : register(t9);
TextureCubeArray g_ShadowAtlasCube : register(t8);
Texture2D<float2> g_VSMShadowAtlas : register(t10);

SamplerState g_Sample : register(s0);
SamplerState g_Sample2 : register(s1);
SamplerComparisonState g_ShadowSample : register(s2);
SamplerState g_VSMSampler : register(s3);

#if PARTICLE_LIGHTING
// 조명 시스템 include
#include "../Common/LightStructures.hlsl"
#include "../Common/LightingBuffers.hlsl"
#include "../Common/LightingCommon.hlsl"
#endif

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT0;
    float4 Color : COLOR;

    float4 InstanceWorld0 : INSTANCE_WORLD0;
    float4 InstanceWorld1 : INSTANCE_WORLD1;
    float4 InstanceWorld2 : INSTANCE_WORLD2;
    float4 InstanceWorld3 : INSTANCE_WORLD3;

    float4 InstanceInvWorld0 : INSTANCE_INVWORLD0;
    float4 InstanceInvWorld1 : INSTANCE_INVWORLD1;
    float4 InstanceInvWorld2 : INSTANCE_INVWORLD2;
    float4 InstanceInvWorld3 : INSTANCE_INVWORLD3;

    float4 InstanceColor : INSTANCE_COLOR;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL0;
    row_major float3x3 TBN : TBN;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
    uint UUID : SV_Target1;
};

//================================================================================================
// Vertex Shader
//================================================================================================
PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Out;

    float4x4 LocalWorldMatrix = float4x4(
        Input.InstanceWorld0,
        Input.InstanceWorld1,
        Input.InstanceWorld2,
        Input.InstanceWorld3
    );

    float4x4 LocalWorldInvT = float4x4(
        Input.InstanceInvWorld0,
        Input.InstanceInvWorld1,
        Input.InstanceInvWorld2,
        Input.InstanceInvWorld3
    );
    
    float3x3 LocalWorldInverseTranspose = (float3x3) LocalWorldInvT;

    // VertexColor * InstanceColor (ColorOverLife 반영)
    float4 CombinedColor = Input.Color * Input.InstanceColor;
    
    float4 WorldPos = mul(float4(Input.Position, 1.0f), LocalWorldMatrix);
    Out.WorldPos = WorldPos.xyz;

    float4 ViewPos = mul(WorldPos, ViewMatrix);
    Out.Position = mul(ViewPos, ProjectionMatrix);

    float3 WorldNormal = normalize(mul(Input.Normal, LocalWorldInverseTranspose));
    Out.Normal = WorldNormal;

    float3x3 WorldRotation = (float3x3) LocalWorldMatrix;
    float3 Tangent = normalize(mul(Input.Tangent.xyz, WorldRotation));
    float3 BiTangent = normalize(cross(WorldNormal, Tangent) * Input.Tangent.w);

    row_major float3x3 TBN;
    TBN._m00_m01_m02 = Tangent;
    TBN._m10_m11_m12 = BiTangent;
    TBN._m20_m21_m22 = WorldNormal;
    Out.TBN = TBN;

    Out.TexCoord = Input.TexCoord;
    Out.Color = CombinedColor;

    return Out;
}

//================================================================================================
// Pixel Shader
//================================================================================================
PS_OUTPUT mainPS(PS_INPUT Input)
{
    PS_OUTPUT Output;
    Output.UUID = UUID;

    // 텍스처 샘플링
    float4 texColor = g_DiffuseTexColor.Sample(g_Sample, Input.TexCoord);
    
    // 베이스 색상 결정
    float4 baseColor = Input.Color;
    if (bHasTexture)
    {
        baseColor*= texColor;
    }
    else if (bHasMaterial)
    {
        baseColor.rgb *= Material.DiffuseColor;
    }

#if PARTICLE_LIGHTING
    // ========================================================================
    // 조명 적용 경로 (Phong Shading)
    // ========================================================================
    
    float3 normal = normalize(Input.Normal);
    
    // Normal Map 적용
    if (bHasNormalTexture)
    {
        normal = g_NormalTexColor.Sample(g_Sample2, Input.TexCoord);
        normal = normal * 2.0f - 1.0f;
        normal = normalize(mul(normal, Input.TBN));
    }
    
    float3 viewDir = normalize(CameraPosition - Input.WorldPos);
    float specPower = bHasMaterial ? Material.SpecularExponent : 32.0f;
    
    float3 litColor = float3(0.0f, 0.0f, 0.0f);

    // Ambient Light
    float3 Ka = bHasMaterial ? Material.AmbientColor : baseColor.rgb;
    bool bIsDefaultKa = all(abs(Ka) < 0.01f) || all(abs(Ka - 1.0f) < 0.01f);
    if (bIsDefaultKa)
    {
        Ka = baseColor.rgb;
    }
    litColor += CalculateAmbientLight(AmbientLight, Ka);

    // Directional Light
    float4 ViewPos = mul(float4(Input.WorldPos, 1), ViewMatrix);
    litColor += CalculateDirectionalLight(
        DirectionalLight, Input.WorldPos, ViewPos.xyz, normal, viewDir,
        baseColor, true, specPower, g_ShadowAtlas2D, g_ShadowSample);

    // Tile Culling 적용
    if (bUseTileCulling)
    {
        uint tileIndex = CalculateTileIndex(Input.Position, ViewportStartX, ViewportStartY);
        uint tileDataOffset = GetTileDataOffset(tileIndex);
        uint lightCount = g_TileLightIndices[tileDataOffset];

        [loop]
        for (uint i = 0; i < lightCount; i++)
        {
            uint packedIndex = g_TileLightIndices[tileDataOffset + 1 + i];
            uint lightType = (packedIndex >> 16) & 0xFFFF;
            uint lightIdx = packedIndex & 0xFFFF;

            if (lightType == 0)  // Point Light
            {
                litColor += CalculatePointLight(
                    g_PointLightList[lightIdx], Input.WorldPos, normal, viewDir,
                    baseColor, true, specPower, g_ShadowAtlasCube, g_ShadowSample);
            }
            else if (lightType == 1)  // Spot Light
            {
                litColor += CalculateSpotLight(
                    g_SpotLightList[lightIdx], Input.WorldPos, normal, viewDir,
                    baseColor, true, specPower, g_ShadowAtlas2D, g_ShadowSample,
                    g_VSMShadowAtlas, g_VSMSampler);
            }
        }
    }
    else
    {
        // 타일 컬링 비활성화: 모든 라이트 순회
        for (int i = 0; i < PointLightCount; i++)
        {
            litColor += CalculatePointLight(
                g_PointLightList[i], Input.WorldPos, normal, viewDir,
                baseColor, true, specPower, g_ShadowAtlasCube, g_ShadowSample);
        }

        [loop]
        for (int j = 0; j < SpotLightCount; j++)
        {
            litColor += CalculateSpotLight(
                g_SpotLightList[j], Input.WorldPos, normal, viewDir,
                baseColor, true, specPower, g_ShadowAtlas2D, g_ShadowSample,
                g_VSMShadowAtlas, g_VSMSampler);
        }
    }

    // 자체발광 추가
    if (bHasMaterial)
    {
        litColor += Material.EmissiveColor;
    }

    float finalAlpha = baseColor.a;
    if (bHasMaterial)
    {
        finalAlpha *= (1.0f - Material.Transparency);
    }

    Output.Color = float4(litColor, finalAlpha);

#else
    // ========================================================================
    // Unlit 경로 (조명 없음)
    // ========================================================================
    float4 finalPixel = baseColor;

    // 자체발광 추가
    if (bHasMaterial)
    {
        finalPixel.rgb += Material.EmissiveColor;
    }

    // 투명도 적용
    if (bHasMaterial)
    {
        finalPixel.a *= (1.0f - Material.Transparency);
    }

    Output.Color = finalPixel;

#endif

    return Output;
}