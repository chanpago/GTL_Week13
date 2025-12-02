// DOF Tile Flatten Compute Shader
// 8x8 타일별 Max CoC 계산 (Near/Far 분리)

#define TILE_SIZE 8

Texture2D<float4> g_InputTex : register(t0);  // 혼합 텍스처 (CoC in alpha, 부호 있음)
RWTexture2D<float4> g_TileOutput : register(u0);

cbuffer TileConstants : register(b0)
{
    uint InputSizeX;
    uint InputSizeY;
    uint TileCountX;
    uint TileCountY;
    float CocRadiusToTileScale;
    float3 _Pad;
};

// Near/Far 분리
groupshared float sharedMaxNearCoC[TILE_SIZE * TILE_SIZE];
groupshared float sharedMaxFarCoC[TILE_SIZE * TILE_SIZE];

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void mainCS(
    uint3 GroupId : SV_GroupID,
    uint3 GroupThreadId : SV_GroupThreadID,
    uint GroupIndex : SV_GroupIndex)
{
    // 픽셀 좌표 계산
    uint2 pixelPos = GroupId.xy * TILE_SIZE + GroupThreadId.xy;

    // Near/Far CoC 분리 (Near: 음수, Far: 양수)
    float nearCoC = 0.0;
    float farCoC = 0.0;
    if (pixelPos.x < InputSizeX && pixelPos.y < InputSizeY)
    {
        float cocSigned = g_InputTex[pixelPos].a;
        if (cocSigned < 0.0)
            nearCoC = abs(cocSigned);  // Near는 절대값으로 저장
        else
            farCoC = cocSigned;
    }

    // Shared memory에 저장
    sharedMaxNearCoC[GroupIndex] = nearCoC;
    sharedMaxFarCoC[GroupIndex] = farCoC;

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction
    [unroll]
    for (uint s = (TILE_SIZE * TILE_SIZE) / 2; s > 0; s >>= 1)
    {
        if (GroupIndex < s)
        {
            sharedMaxNearCoC[GroupIndex] = max(sharedMaxNearCoC[GroupIndex], sharedMaxNearCoC[GroupIndex + s]);
            sharedMaxFarCoC[GroupIndex] = max(sharedMaxFarCoC[GroupIndex], sharedMaxFarCoC[GroupIndex + s]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0이 결과 저장
    if (GroupIndex == 0)
    {
        // x: MaxNearCoC, y: MaxFarCoC
        g_TileOutput[GroupId.xy] = float4(sharedMaxNearCoC[0], sharedMaxFarCoC[0], 0.0, 0.0);
    }
}
