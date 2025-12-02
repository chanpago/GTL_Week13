#include "pch.h"
#include "DOFTilePass.h"
#include "../SceneView.h"
#include "../../RHI/D3D11RHI.h"

// Constant Buffer 구조체 (셰이더와 일치해야 함)
struct FTileConstants
{
    uint32 InputSizeX;
    uint32 InputSizeY;
    uint32 TileCountX;
    uint32 TileCountY;
    float CocRadiusToTileScale;
    float _Pad[3];
};

FDOFTilePass::~FDOFTilePass()
{
    Release();
}

void FDOFTilePass::Initialize(D3D11RHI* RHIDevice, UINT InInputWidth, UINT InInputHeight)
{
    if (bInitialized)
    {
        // 크기가 변경되면 재생성
        if (InputWidth != InInputWidth || InputHeight != InInputHeight)
        {
            Release();
        }
        else
        {
            return;  // 이미 초기화됨
        }
    }

    InputWidth = InInputWidth;
    InputHeight = InInputHeight;
    TileCountX = (InputWidth + TILE_SIZE - 1) / TILE_SIZE;
    TileCountY = (InputHeight + TILE_SIZE - 1) / TILE_SIZE;

    CreateResources(RHIDevice);
    bInitialized = true;

    UE_LOG("DOFTilePass: Initialized (Input: %dx%d, Tiles: %dx%d)\n",
           InputWidth, InputHeight, TileCountX, TileCountY);
}

void FDOFTilePass::Release()
{
    ReleaseResources();
    bInitialized = false;
}

void FDOFTilePass::CreateResources(D3D11RHI* RHIDevice)
{
    // 타일 텍스처 포맷: RG32F (MinCoC, MaxCoC)
    // 나중에 Near/Far 분리하려면 RGBA32F로 확장
    DXGI_FORMAT tileFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

    // Flattened Tile 텍스처
    HRESULT hr = RHIDevice->CreateUAVTexture2D(
        TileCountX, TileCountY, tileFormat,
        &FlattenedTileTexture, &FlattenedTileSRV, &FlattenedTileUAV);

    if (FAILED(hr))
    {
        UE_LOG("[error] DOFTilePass: Failed to create FlattenedTile texture\n");
        return;
    }

    // Dilated Tile 텍스처
    hr = RHIDevice->CreateUAVTexture2D(
        TileCountX, TileCountY, tileFormat,
        &DilatedTileTexture, &DilatedTileSRV, &DilatedTileUAV);

    if (FAILED(hr))
    {
        UE_LOG("[error] DOFTilePass: Failed to create DilatedTile texture\n");
        return;
    }

    // Constant Buffer 생성
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(FTileConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = RHIDevice->GetDevice()->CreateBuffer(&cbDesc, nullptr, &TileConstantBuffer);
    if (FAILED(hr))
    {
        UE_LOG("[error] DOFTilePass: Failed to create constant buffer\n");
    }
}

void FDOFTilePass::ReleaseResources()
{
    if (FlattenedTileUAV) { FlattenedTileUAV->Release(); FlattenedTileUAV = nullptr; }
    if (FlattenedTileSRV) { FlattenedTileSRV->Release(); FlattenedTileSRV = nullptr; }
    if (FlattenedTileTexture) { FlattenedTileTexture->Release(); FlattenedTileTexture = nullptr; }

    if (DilatedTileUAV) { DilatedTileUAV->Release(); DilatedTileUAV = nullptr; }
    if (DilatedTileSRV) { DilatedTileSRV->Release(); DilatedTileSRV = nullptr; }
    if (DilatedTileTexture) { DilatedTileTexture->Release(); DilatedTileTexture = nullptr; }

    if (TileConstantBuffer) { TileConstantBuffer->Release(); TileConstantBuffer = nullptr; }
}

void FDOFTilePass::Execute(D3D11RHI* RHIDevice)
{
    // DOF 텍스처 크기 (1/2 해상도)
    UINT dofWidth = RHIDevice->GetSwapChainWidth() / 2;
    UINT dofHeight = RHIDevice->GetSwapChainHeight() / 2;

    // 초기화 (최초 1회 또는 크기 변경 시)
    Initialize(RHIDevice, dofWidth, dofHeight);

    if (!bInitialized)
    {
        return;
    }

    // 혼합 텍스처 SRV 가져오기 (Near/Far 통합)
    ID3D11ShaderResourceView* InputSRV = RHIDevice->GetDOFSRV(0);

    if (!InputSRV)
    {
        return;
    }

    auto* DeviceContext = RHIDevice->GetDeviceContext();

    // =====================================================
    // Pass 1: Tile Flatten
    // =====================================================
    UShader* FlattenCS = UResourceManager::GetInstance().Load<UShader>(
        "Shaders/PostProcess/DOF_TileFlatten_CS.hlsl");

    if (!FlattenCS || !FlattenCS->GetComputeShader())
    {
        UE_LOG("[error] DOFTilePass: Failed to load TileFlatten compute shader\n");
        return;
    }

    // Constant Buffer 업데이트
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        DeviceContext->Map(TileConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

        FTileConstants* cb = (FTileConstants*)mapped.pData;
        cb->InputSizeX = InputWidth;
        cb->InputSizeY = InputHeight;
        cb->TileCountX = TileCountX;
        cb->TileCountY = TileCountY;
        cb->CocRadiusToTileScale = 32.0f;  // CoC(0~1) * 32 = Half Res 픽셀 (최대 블러 64px Full Res 기준)

        DeviceContext->Unmap(TileConstantBuffer, 0);
    }

    // Flatten CS 설정
    RHIDevice->CSSetShader(FlattenCS->GetComputeShader());
    RHIDevice->CSSetConstantBuffers(0, 1, &TileConstantBuffer);

    // 입력: 혼합 텍스처 (Near/Far 통합, CoC 부호 있음)
    RHIDevice->CSSetShaderResources(0, 1, &InputSRV);

    // 출력: Flattened Tile 텍스처
    RHIDevice->CSSetUnorderedAccessViews(0, 1, &FlattenedTileUAV, nullptr);

    // Dispatch (타일 개수만큼의 그룹, 각 그룹 8x8 스레드)
    RHIDevice->Dispatch(TileCountX, TileCountY, 1);

    // 리소스 언바인드
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    RHIDevice->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    RHIDevice->CSSetShaderResources(0, 1, &nullSRV);

    // =====================================================
    // Pass 2: Tile Dilate
    // =====================================================
    UShader* DilateCS = UResourceManager::GetInstance().Load<UShader>(
        "Shaders/PostProcess/DOF_TileDilate_CS.hlsl");

    if (!DilateCS || !DilateCS->GetComputeShader())
    {
        UE_LOG("[error] DOFTilePass: Failed to load TileDilate compute shader\n");
        return;
    }

    // Dilate CS 설정
    RHIDevice->CSSetShader(DilateCS->GetComputeShader());
    RHIDevice->CSSetConstantBuffers(0, 1, &TileConstantBuffer);

    // 입력: Flattened Tile
    RHIDevice->CSSetShaderResources(0, 1, &FlattenedTileSRV);

    // 출력: Dilated Tile
    RHIDevice->CSSetUnorderedAccessViews(0, 1, &DilatedTileUAV, nullptr);

    // Dispatch (타일당 1스레드 그룹, 8x8 스레드)
    UINT dilateGroupsX = (TileCountX + 7) / 8;
    UINT dilateGroupsY = (TileCountY + 7) / 8;
    RHIDevice->Dispatch(dilateGroupsX, dilateGroupsY, 1);

    // 정리
    RHIDevice->UnbindComputeResources();
}
