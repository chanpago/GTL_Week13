// DOF Tile Dilate Compute Shader
// 이웃 타일로 Max CoC 확장 (Near/Far 분리)

#define TILE_SIZE 8
#define DILATE_RADIUS 4  // 4타일 = 32픽셀 (TILE_SIZE=8)

Texture2D<float4> g_TileInput : register(t0);   // Flatten 결과 (x: MaxNearCoC, y: MaxFarCoC)
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

[numthreads(8, 8, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= TileCountX || DTid.y >= TileCountY)
        return;

    // 현재 타일의 Near/Far Max CoC
    float4 centerTile = g_TileInput[DTid.xy];
    float maxNearCoC = centerTile.x;
    float maxFarCoC = centerTile.y;

    // Ring별로 이웃 타일 확인
    [unroll]
    for (int ring = 1; ring <= DILATE_RADIUS; ring++)
    {
        float ringDistance = float(ring - 1) * TILE_SIZE;

        for (int y = -ring; y <= ring; y++)
        {
            for (int x = -ring; x <= ring; x++)
            {
                if (abs(x) != ring && abs(y) != ring)
                    continue;

                int2 neighborPos = int2(DTid.xy) + int2(x, y);

                if (neighborPos.x < 0 || neighborPos.y < 0 ||
                    neighborPos.x >= (int)TileCountX || neighborPos.y >= (int)TileCountY)
                    continue;

                float4 neighborTile = g_TileInput[neighborPos];
                float neighborNearCoC = neighborTile.x;
                float neighborFarCoC = neighborTile.y;

                // Near CoC 딜레이트
                if (neighborNearCoC * CocRadiusToTileScale >= ringDistance)
                {
                    maxNearCoC = max(maxNearCoC, neighborNearCoC);
                }

                // Far CoC 딜레이트
                if (neighborFarCoC * CocRadiusToTileScale >= ringDistance)
                {
                    maxFarCoC = max(maxFarCoC, neighborFarCoC);
                }
            }
        }
    }

    // x: Dilated MaxNearCoC, y: Dilated MaxFarCoC
    g_TileOutput[DTid.xy] = float4(maxNearCoC, maxFarCoC, 0.0, 0.0);
}
