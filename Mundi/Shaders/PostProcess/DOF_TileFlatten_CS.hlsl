// DOF Tile Flatten Compute Shader
// 8x8 타일별 Max CoC 계산 (Near/Far 혼합, 절대값 사용)

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

// Max/Min CoC (절대값)
groupshared float sharedMaxCoC[TILE_SIZE * TILE_SIZE];
groupshared float sharedMinCoC[TILE_SIZE * TILE_SIZE];

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void mainCS(
    uint3 GroupId : SV_GroupID,
    uint3 GroupThreadId : SV_GroupThreadID,
    uint GroupIndex : SV_GroupIndex)
{
    // 픽셀 좌표 계산
    uint2 pixelPos = GroupId.xy * TILE_SIZE + GroupThreadId.xy;

    // 경계 체크 및 CoC 읽기 (절대값)
    float cocAbs = 0.0;
    if (pixelPos.x < InputSizeX && pixelPos.y < InputSizeY)
    {
        float cocSigned = g_InputTex[pixelPos].a;  // Near: 음수, Far: 양수
        cocAbs = abs(cocSigned);
    }

    // Shared memory에 저장
    sharedMaxCoC[GroupIndex] = cocAbs;
    sharedMinCoC[GroupIndex] = (cocAbs > 0.001) ? cocAbs : 999999.0;

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction (64 -> 32 -> 16 -> 8 -> 4 -> 2 -> 1)
    [unroll]
    for (uint s = (TILE_SIZE * TILE_SIZE) / 2; s > 0; s >>= 1)
    {
        if (GroupIndex < s)
        {
            sharedMaxCoC[GroupIndex] = max(sharedMaxCoC[GroupIndex], sharedMaxCoC[GroupIndex + s]);
            sharedMinCoC[GroupIndex] = min(sharedMinCoC[GroupIndex], sharedMinCoC[GroupIndex + s]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0이 결과 저장
    if (GroupIndex == 0)
    {
        float minCoC = (sharedMinCoC[0] < 999999.0) ? sharedMinCoC[0] : 0.0;

        // x: MaxCoC, y: MinCoC, z,w: unused
        g_TileOutput[GroupId.xy] = float4(sharedMaxCoC[0], minCoC, 0.0, 0.0);
    }
}
