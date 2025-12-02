// DOF Tile Dilate Compute Shader
// 이웃 타일로 Max CoC 확장 (Near/Far 통합)

#define TILE_SIZE 8
#define DILATE_RADIUS 4  // 4타일 = 32픽셀 (TILE_SIZE=8)

Texture2D<float4> g_TileInput : register(t0);   // Flatten 결과 (x: MaxCoC, y: MinCoC)
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

    // 현재 타일의 Min/Max CoC
    float4 centerTile = g_TileInput[DTid.xy];
    float maxCoC = centerTile.x;
    float minCoC = centerTile.y;

    // Ring별로 이웃 타일 확인
    [unroll]
    for (int ring = 1; ring <= DILATE_RADIUS; ring++)
    {
        float ringDistance = float(ring - 1) * TILE_SIZE;  // 타일 경계까지 거리 (픽셀)

        // 정사각형 링 패턴으로 순회
        for (int y = -ring; y <= ring; y++)
        {
            for (int x = -ring; x <= ring; x++)
            {
                // 링 외곽만 (내부는 이미 처리됨)
                if (abs(x) != ring && abs(y) != ring)
                    continue;

                int2 neighborPos = int2(DTid.xy) + int2(x, y);

                // 경계 체크
                if (neighborPos.x < 0 || neighborPos.y < 0 ||
                    neighborPos.x >= (int)TileCountX || neighborPos.y >= (int)TileCountY)
                    continue;

                float4 neighborTile = g_TileInput[neighborPos];
                float neighborMaxCoC = neighborTile.x;
                float neighborMinCoC = neighborTile.y;

                // 이웃의 CoC가 이 거리까지 도달할 수 있는지 체크 (>= 로 경계 포함)
                if (neighborMaxCoC * CocRadiusToTileScale >= ringDistance)
                {
                    maxCoC = max(maxCoC, neighborMaxCoC);
                    // Min은 유효한 값만 (0보다 큰 경우)
                    if (neighborMinCoC > 0.001)
                    {
                        minCoC = (minCoC > 0.001) ? min(minCoC, neighborMinCoC) : neighborMinCoC;
                    }
                }
            }
        }
    }

    // 결과 저장
    // x: Dilated MaxCoC, y: Dilated MinCoC, z,w: unused
    g_TileOutput[DTid.xy] = float4(maxCoC, minCoC, 0.0, 0.0);
}
