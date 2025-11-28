#include "pch.h"
#include "StatsOverlayD2D.h"
#include "Color.h"

void D3D11RHI::Initialize(HWND hWindow)
{
    // 이곳에서 Device, DeviceContext, viewport, swapchain를 초기화한다
    CreateDeviceAndSwapChain(hWindow);
    CreateFrameBuffer();
    CreateIdBuffer();
    CreateDOFResources();  // DOF 렌더 타겟 생성
    CreateRasterizerState();
    CreateBlendState();
    CONSTANT_BUFFER_LIST(CREATE_CONSTANT_BUFFER);

	CreateDepthStencilState();
	CreateSamplerState();
    UResourceManager::GetInstance().Initialize(Device,DeviceContext);

    // Initialize Direct2D overlay after device/swapchain ready
    UStatsOverlayD2D::Get().Initialize(Device, DeviceContext, SwapChain);
}

void D3D11RHI::Release()
{
    // Prevent double Release() calls
    if (bReleased) return;
    bReleased = true;

    // Direct2D 오버레이를 먼저 정리하여 D3D 리소스에 대한 참조를 제거
    UStatsOverlayD2D::Get().Shutdown();

    if (DeviceContext)
    {
        // 파이프라인에서 바인딩된 상태/리소스를 명시적으로 해제
        DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        DeviceContext->OMSetDepthStencilState(nullptr, 0);
        DeviceContext->RSSetState(nullptr);
        DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);

        // 모든 SRV 언바인드
        ID3D11ShaderResourceView* nullSRVs[32] = { nullptr };
        DeviceContext->PSSetShaderResources(0, 32, nullSRVs);
        DeviceContext->VSSetShaderResources(0, 32, nullSRVs);

        // 모든 샘플러 언바인드
        ID3D11SamplerState* nullSamplers[16] = { nullptr };
        DeviceContext->PSSetSamplers(0, 16, nullSamplers);
        DeviceContext->VSSetSamplers(0, 16, nullSamplers);

        DeviceContext->ClearState();
        DeviceContext->Flush();
    }

    ReleaseSamplerState();

    // 상수버퍼
    CONSTANT_BUFFER_LIST(RELEASE_CONSTANT_BUFFER);

    // 상태 객체
    if (DepthStencilState) { DepthStencilState->Release(); DepthStencilState = nullptr; }
    if (DepthStencilStateLessEqualWrite) { DepthStencilStateLessEqualWrite->Release(); DepthStencilStateLessEqualWrite = nullptr; }
    if (DepthStencilStateLessEqualReadOnly) { DepthStencilStateLessEqualReadOnly->Release(); DepthStencilStateLessEqualReadOnly = nullptr; }
    if (DepthStencilStateAlwaysNoWrite) { DepthStencilStateAlwaysNoWrite->Release(); DepthStencilStateAlwaysNoWrite = nullptr; }
    if (DepthStencilStateDisable) { DepthStencilStateDisable->Release(); DepthStencilStateDisable = nullptr; }
    if (DepthStencilStateGreaterEqualWrite) { DepthStencilStateGreaterEqualWrite->Release(); DepthStencilStateGreaterEqualWrite = nullptr; }
    if (DepthStencilStateOverlayWriteStencil) { DepthStencilStateOverlayWriteStencil->Release(); DepthStencilStateOverlayWriteStencil = nullptr; }
    if (DepthStencilStateStencilRejectOverlay) { DepthStencilStateStencilRejectOverlay->Release(); DepthStencilStateStencilRejectOverlay = nullptr; }

    if (DefaultRasterizerState) { DefaultRasterizerState->Release();   DefaultRasterizerState = nullptr; }
    if (WireFrameRasterizerState) { WireFrameRasterizerState->Release();   WireFrameRasterizerState = nullptr; }
    if (DecalRasterizerState) { DecalRasterizerState->Release();   DecalRasterizerState = nullptr; }
    if (ShadowRasterizerState) { ShadowRasterizerState->Release();   ShadowRasterizerState = nullptr; }
    if (NoCullRasterizerState) { NoCullRasterizerState->Release();   NoCullRasterizerState = nullptr; }

    ReleaseBlendState();
    // RTV/DSV/FrameBuffer
    ReleaseFrameBuffer();
    ReleaseIdBuffer();
    ReleaseDOFResources();  // DOF 렌더 타겟 해제

    // Device + SwapChain
    ReleaseDeviceAndSwapChain();
}

void D3D11RHI::ClearAllBuffer()
{
    float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float ClearId[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    DeviceContext->ClearRenderTargetView(BackBufferRTV, ClearColor);
    DeviceContext->ClearRenderTargetView(GetCurrentTargetRTV(), ClearId);
    DeviceContext->ClearRenderTargetView(IdBufferRTV, ClearId);
    
    ClearDepthBuffer(1.0f, 0);                 // 깊이값 초기화
}

void D3D11RHI::ClearDepthBuffer(float Depth, UINT Stencil)
{
    DeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, Depth, Stencil);
}

void D3D11RHI::CreateBlendState()
{
    // Create once; reuse every frame
    if (BlendStateOpaque)
        return;

    D3D11_BLEND_DESC BlendDesc = {};
    
    BlendDesc.IndependentBlendEnable = true;

    D3D11_RENDER_TARGET_BLEND_DESC& Rt0 = BlendDesc.RenderTarget[0];

    Rt0.BlendEnable = TRUE;
    Rt0.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    Rt0.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    Rt0.BlendOp = D3D11_BLEND_OP_ADD;
    Rt0.SrcBlendAlpha = D3D11_BLEND_ONE;
    Rt0.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    Rt0.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    Rt0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;


    D3D11_RENDER_TARGET_BLEND_DESC& Rt1 = BlendDesc.RenderTarget[1];
    Rt1.BlendEnable = FALSE;
    Rt1.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    Device->CreateBlendState(&BlendDesc, &BlendStateTransparent);

    BlendDesc = {};
    BlendDesc.IndependentBlendEnable = TRUE;
    Rt0 = BlendDesc.RenderTarget[0];
    Rt0.BlendEnable = FALSE;
    Rt0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    Rt1 = BlendDesc.RenderTarget[1];
    Rt1.BlendEnable = FALSE;
    Rt1.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    Device->CreateBlendState(&BlendDesc, &BlendStateOpaque);
    //auto& rt = bd.RenderTarget[0];
    //rt.BlendEnable = TRUE;
    //rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;      // 스트레이트 알파
    //rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;  // (프리멀티면 ONE / INV_SRC_ALPHA)
    //rt.BlendOp = D3D11_BLEND_OP_ADD;
    //rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    //rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    //rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    //rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    //Device->CreateBlendState(&bd, &BlendState);
}

void D3D11RHI::CreateDepthStencilState()
{
    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.StencilEnable = FALSE;

    // 1) 기본: LessEqual + Write ALL
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    Device->CreateDepthStencilState(&desc, &DepthStencilStateLessEqualWrite);

    // 2) ReadOnly: LessEqual + Write ZERO
    desc.DepthEnable = TRUE;                            // 깊이 테스트를 켭니다.
    desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;       // 일반적인 '가깝거나 같으면 통과' 규칙을 사용합니다.
    // 3. 뎁스 쓰기 관련 설정을 합니다. (핵심 부분)
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // 깊이 버퍼에 쓰지 않도록 설정합니다 (OFF).
    // 4. 스텐실 테스트는 사용하지 않습니다.
    desc.StencilEnable = FALSE;
    Device->CreateDepthStencilState(&desc, &DepthStencilStateLessEqualReadOnly);

    // 3) AlwaysNoWrite: Always + Write ZERO (기즈모/오버레이 용)
    desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    // DepthEnable은 TRUE 유지 (읽기 의미는 없지만 상태 일관성을 위해)
    Device->CreateDepthStencilState(&desc, &DepthStencilStateAlwaysNoWrite);

    // 4) Disable: DepthEnable FALSE (테스트/쓰기 모두 무시)
    desc.DepthEnable = FALSE;
    // DepthWriteMask/Func는 무시되지만 값은 그대로 둬도 됨
    Device->CreateDepthStencilState(&desc, &DepthStencilStateDisable);

    // 5) (선택) GreaterEqual + Write ALL
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    Device->CreateDepthStencilState(&desc, &DepthStencilStateGreaterEqualWrite);

    // 6) OverlayWriteStencil: Always + NoWriteDepth + Stencil=REPLACE 1
    ZeroMemory(&desc, sizeof(desc));
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    desc.StencilEnable = TRUE;
    desc.StencilReadMask = 0xFF;
    desc.StencilWriteMask = 0xFF;
    desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE; // write ref
    desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    desc.BackFace = desc.FrontFace;
    Device->CreateDepthStencilState(&desc, &DepthStencilStateOverlayWriteStencil);

    // 7) StencilRejectOverlay: LessEqual + DepthWrite ALL + Stencil EQUAL 0 (keep)
    ZeroMemory(&desc, sizeof(desc));
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    desc.StencilEnable = TRUE;
    desc.StencilReadMask = 0xFF;
    desc.StencilWriteMask = 0x00; // no stencil write
    desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL; // pass only when stencil==ref
    desc.BackFace = desc.FrontFace;
    Device->CreateDepthStencilState(&desc, &DepthStencilStateStencilRejectOverlay);
}

void D3D11RHI::CreateSamplerState()
{
    D3D11_SAMPLER_DESC SampleDesc = {};
    SampleDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    SampleDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    SampleDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    SampleDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    SampleDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    SampleDesc.MinLOD = 0;
    SampleDesc.MaxLOD = D3D11_FLOAT32_MAX;

	HRESULT HR = Device->CreateSamplerState(&SampleDesc, &DefaultSamplerState);

    // Linear Clamp Sampler
    D3D11_SAMPLER_DESC LinearClampDesc = {};
    LinearClampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    LinearClampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    LinearClampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    LinearClampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    LinearClampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    LinearClampDesc.MinLOD = 0;
    LinearClampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HR = Device->CreateSamplerState(&LinearClampDesc, &LinearClampSamplerState);

	// Point Clamp Sampler
	D3D11_SAMPLER_DESC PointClampDesc = {};
	PointClampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	PointClampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	PointClampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	PointClampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	PointClampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	PointClampDesc.MinLOD = 0;
	PointClampDesc.MaxLOD = D3D11_FLOAT32_MAX;

	HR = Device->CreateSamplerState(&PointClampDesc, &PointClampSamplerState);

    D3D11_SAMPLER_DESC ShadowSamplerDesc = {};
    ShadowSamplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    ShadowSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    ShadowSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    ShadowSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    ShadowSamplerDesc.BorderColor[0] = 1.0f;
    ShadowSamplerDesc.BorderColor[1] = 1.0f;
    ShadowSamplerDesc.BorderColor[2] = 1.0f;
    ShadowSamplerDesc.BorderColor[3] = 1.0f;
    ShadowSamplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;

    HR = Device->CreateSamplerState(&ShadowSamplerDesc, &ShadowSamplerState);

    D3D11_SAMPLER_DESC VSMSamplerDesc = {};
    VSMSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    VSMSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    VSMSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    VSMSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    VSMSamplerDesc.BorderColor[0] = 1.0f;
    VSMSamplerDesc.BorderColor[1] = 1.0f;
    VSMSamplerDesc.BorderColor[2] = 1.0f;
    VSMSamplerDesc.BorderColor[3] = 1.0f;
    VSMSamplerDesc.MipLODBias = 0.0f;
    VSMSamplerDesc.MaxAnisotropy = 1;
    VSMSamplerDesc.MinLOD = 0;
    VSMSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HR = Device->CreateSamplerState(&VSMSamplerDesc, &VSMSamplerState);
}

HRESULT D3D11RHI::CreateIndexBuffer(ID3D11Device* device, const FMeshData* meshData, ID3D11Buffer** outBuffer)
{
    if (!meshData || meshData->Indices.empty())
        return E_FAIL;

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32) * meshData->Indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA iinitData = {};
    iinitData.pSysMem = meshData->Indices.data();

    return device->CreateBuffer(&ibd, &iinitData, outBuffer);
}

HRESULT D3D11RHI::CreateIndexBuffer(ID3D11Device* device, const FStaticMesh* mesh, ID3D11Buffer** outBuffer)
{
    if (!mesh || mesh->Indices.empty())
        return E_FAIL;

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32) * mesh->Indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA iinitData = {};
    iinitData.pSysMem = mesh->Indices.data();

    return device->CreateBuffer(&ibd, &iinitData, outBuffer);
}

HRESULT D3D11RHI::CreateIndexBuffer(ID3D11Device* Device, const FSkeletalMeshData* Mesh, ID3D11Buffer** OutBuffer)
{
    if (!Mesh || Mesh->Indices.empty())
        return E_FAIL;

    D3D11_BUFFER_DESC IndexBufferDesc = {};
    IndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    IndexBufferDesc.ByteWidth = static_cast<UINT>(sizeof(uint32) * Mesh->Indices.size());
    IndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    IndexBufferDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData = {};
    InitData.pSysMem = Mesh->Indices.data();

    return Device->CreateBuffer(&IndexBufferDesc, &InitData, OutBuffer);
}

void D3D11RHI::ConstantBufferSet(ID3D11Buffer* ConstantBuffer, uint32 Slot, bool bIsVS, bool bIsPS)
{
    if (bIsVS)
    {
        DeviceContext->VSSetConstantBuffers(Slot, 1, &ConstantBuffer);
    }
    if (bIsPS)
    {
        DeviceContext->PSSetConstantBuffers(Slot, 1, &ConstantBuffer);
    }
}


void D3D11RHI::IASetPrimitiveTopology()
{
    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void D3D11RHI::RSSetState(ERasterizerMode ViewMode)
{
	switch (ViewMode)
	{
	case ERasterizerMode::Solid:
		DeviceContext->RSSetState(DefaultRasterizerState);
        break;

	case ERasterizerMode::Wireframe:
		DeviceContext->RSSetState(WireFrameRasterizerState);
        break;

	case ERasterizerMode::Solid_NoCull:
		DeviceContext->RSSetState(NoCullRasterizerState);
        break;

	case ERasterizerMode::Decal:
		DeviceContext->RSSetState(DecalRasterizerState);
        break;

	case ERasterizerMode::Shadows:
		DeviceContext->RSSetState(ShadowRasterizerState);
        break;

	default:
		DeviceContext->RSSetState(DefaultRasterizerState);
        break;
	}
}

void D3D11RHI::RSSetViewport()
{
    DeviceContext->RSSetViewports(1, &ViewportInfo);
}

void D3D11RHI::SwapRenderTargets() // 이전의 SwapPostProcessTextures
{
    std::swap(SourceIndex, TargetIndex);
}

ID3D11RenderTargetView* D3D11RHI::GetCurrentTargetRTV() const
{
    return SceneColorRTVs[TargetIndex];
}

ID3D11DepthStencilView* D3D11RHI::GetSceneDSV() const
{
    return DepthStencilView;
}

ID3D11ShaderResourceView* D3D11RHI::GetCurrentSourceSRV() const
{
    return SceneColorSRVs[SourceIndex];
}

ID3D11ShaderResourceView* D3D11RHI::GetSRV(RHI_SRV_Index SRVIndex) const
{
    ID3D11ShaderResourceView* TempSRV;
    switch (SRVIndex)
    {
    case RHI_SRV_Index::SceneColorSource:
        TempSRV = GetCurrentSourceSRV();
        break;
    case RHI_SRV_Index::SceneDepth:
        TempSRV = DepthSRV;
        break;
    case RHI_SRV_Index::DOFFar:
        TempSRV = DOFSRVs[0];
        break;
    case RHI_SRV_Index::DOFNear:
        TempSRV = DOFSRVs[1];
        break;
    case RHI_SRV_Index::DOFTempH:
        TempSRV = DOFSRVs[2];
        break;
    case RHI_SRV_Index::DOFTempV:
        TempSRV = DOFSRVs[3];
        break;
    default:
        TempSRV = nullptr;
        break;
    }

    return TempSRV;
}

void D3D11RHI::OMSetCustomRenderTargets(UINT NumRTVs, ID3D11RenderTargetView** RTVs, ID3D11DepthStencilView* DSV)
{
    DeviceContext->OMSetRenderTargets(NumRTVs, RTVs, DSV);
}

void D3D11RHI::OMSetRenderTargets(ERTVMode RTVMode)
{
    switch (RTVMode)
    {
    case ERTVMode::BackBufferWithDepth:
        DeviceContext->OMSetRenderTargets(1, &BackBufferRTV, DepthStencilView);
        break;
    case ERTVMode::BackBufferWithoutDepth:
        DeviceContext->OMSetRenderTargets(1, &BackBufferRTV, nullptr);
        break;
    case ERTVMode::SceneColorTarget:
    {
        ID3D11RenderTargetView* CurrentTargetRTV = GetCurrentTargetRTV();
        DeviceContext->OMSetRenderTargets(1, &CurrentTargetRTV, DepthStencilView);
        break;
    }
    case ERTVMode::SceneIdTarget:
    {
        ID3D11RenderTargetView* RTVList[2]{ nullptr, IdBufferRTV };
        DeviceContext->OMSetRenderTargets(2, RTVList, DepthStencilView);
        break;
    }
    case ERTVMode::SceneColorTargetWithId:
    {
        ID3D11RenderTargetView* RTVList[2]{ GetCurrentTargetRTV(), IdBufferRTV };
        DeviceContext->OMSetRenderTargets(2, RTVList, DepthStencilView);
        break;
    }
    case ERTVMode::SceneColorTargetWithoutDepth:
    {
        ID3D11RenderTargetView* CurrentTargetRTV = GetCurrentTargetRTV();
        DeviceContext->OMSetRenderTargets(1, &CurrentTargetRTV, nullptr);
        break;
    }
    default:
        break;
    }
}

void D3D11RHI::OMSetBlendState(bool bIsBlendMode)
{
    if (bIsBlendMode == true)
    {
        float blendFactor[4] = { 0, 0, 0, 0 };
        DeviceContext->OMSetBlendState(BlendStateTransparent, blendFactor, 0xffffffff);
    }
    else
    {
        DeviceContext->OMSetBlendState(BlendStateOpaque, nullptr, 0xffffffff);
    }
}

// 전체 화면 사각형 그리기
void D3D11RHI::DrawFullScreenQuad()
{
    // 1. 입력 버퍼를 사용하지 않겠다고 명시적으로 설정합니다.
    //    Input Assembler (IA) 단계가 사실상 생략됩니다.
    DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    DeviceContext->IASetInputLayout(nullptr); // Input Layout도 필요 없습니다.
    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 2. 정점 셰이더를 6번 실행하여 큰 삼각형 2개를 그리도록 명령합니다.
    DeviceContext->Draw(6, 0);
}

void D3D11RHI::Present()
{
    // Draw any Direct2D overlays before present
    UStatsOverlayD2D::Get().Draw();
    SwapChain->Present(0, 0); // vsync on
}

void D3D11RHI::CreateDeviceAndSwapChain(HWND hWindow)
{
    // 지원하는 Direct3D 기능 레벨을 정의
    D3D_FEATURE_LEVEL featurelevels[] = { D3D_FEATURE_LEVEL_11_0 };

    // 스왑 체인 설정 구조체 초기화
    DXGI_SWAP_CHAIN_DESC swapchaindesc = {};
    swapchaindesc.BufferDesc.Width = 0; // 창 크기에 맞게 자동으로 설정
    swapchaindesc.BufferDesc.Height = 0; // 창 크기에 맞게 자동으로 설정
    swapchaindesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 색상 포맷
    swapchaindesc.SampleDesc.Count = 1; // 멀티 샘플링 비활성화
    swapchaindesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 렌더 타겟으로 사용
    swapchaindesc.BufferCount = 2; // 더블 버퍼링
    swapchaindesc.OutputWindow = hWindow; // 렌더링할 창 핸들
    swapchaindesc.Windowed = TRUE; // 창 모드
    swapchaindesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 스왑 방식

    // Direct3D 장치와 스왑 체인을 생성
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags,
        featurelevels, ARRAYSIZE(featurelevels), D3D11_SDK_VERSION,
        &swapchaindesc, &SwapChain, &Device, nullptr, &DeviceContext);
    // 생성된 스왑 체인의 정보 가져오기
    SwapChain->GetDesc(&swapchaindesc);

    // 뷰포트 정보 설정
    ViewportInfo = { 0.0f, 0.0f, (float)swapchaindesc.BufferDesc.Width, (float)swapchaindesc.BufferDesc.Height, 0.0f, 1.0f };
}

void D3D11RHI::CreateFrameBuffer()
{
    DXGI_SWAP_CHAIN_DESC swapDesc;
    SwapChain->GetDesc(&swapDesc);

    // 백 버퍼 가져오기
    SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&FrameBuffer);

    // 렌더 타겟 뷰 생성
    D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
    framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    Device->CreateRenderTargetView(FrameBuffer, &framebufferRTVdesc, &BackBufferRTV);

    // =====================================
    // 핑퐁(ping-pong) 버퍼 텍스처 생성 (SRV 지원)
    // =====================================
    D3D11_TEXTURE2D_DESC SceneDesc = {};
    SceneDesc.Width = swapDesc.BufferDesc.Width;
    SceneDesc.Height = swapDesc.BufferDesc.Height;
    SceneDesc.MipLevels = 1;
    SceneDesc.ArraySize = 1;
    SceneDesc.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
    SceneDesc.SampleDesc.Count = 1;
    SceneDesc.SampleDesc.Quality = 0;
    SceneDesc.Usage = D3D11_USAGE_DEFAULT;
    SceneDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    SceneDesc.CPUAccessFlags = 0;
    SceneDesc.MiscFlags = 0;

    for (uint32 Idx = 0; Idx < 2; ++Idx)
    {
        HRESULT Result = Device->CreateTexture2D(&SceneDesc, nullptr, &SceneColorTextures[Idx]);
        if (FAILED(Result))
        {
            UE_LOG("DeviceResources: FrameBuffer Texture 생성 실패");
            return;
        }

        D3D11_RENDER_TARGET_VIEW_DESC RtvDesc = {};
        RtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        RtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        RtvDesc.Texture2D.MipSlice = 0;

        Result = Device->CreateRenderTargetView(SceneColorTextures[Idx], &RtvDesc, &SceneColorRTVs[Idx]);
        if (FAILED(Result))
        {
            UE_LOG("DeviceResources: FrameBuffer RTV 생성 실패");
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels = 1;

        Result = Device->CreateShaderResourceView(SceneColorTextures[Idx], &SRVDesc, &SceneColorSRVs[Idx]);
        if (FAILED(Result))
        {
            UE_LOG("DeviceResources: FrameBuffer SRV 생성 실패");
        }
    }

    // =====================================
    // 깊이/스텐실 버퍼 생성 (SRV 지원)
    // =====================================

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = swapDesc.BufferDesc.Width;
    depthDesc.Height = swapDesc.BufferDesc.Height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS; // Typeless 포맷으로 변경
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE; // SRV도 바인딩 가능하게
    depthDesc.CPUAccessFlags = 0;

    Device->CreateTexture2D(&depthDesc, nullptr, &DepthBuffer);

    // DepthStencilView 생성
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // DSV용 포맷
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    Device->CreateDepthStencilView(DepthBuffer, &dsvDesc, &DepthStencilView);

    // DepthSRV 생성
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; // SRV용 포맷 (depth만 읽음)
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    Device->CreateShaderResourceView(DepthBuffer, &srvDesc, &DepthSRV);
}

void D3D11RHI::CreateIdBuffer()
{

    DXGI_SWAP_CHAIN_DESC SwapDesc;
    SwapChain->GetDesc(&SwapDesc);

    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Format = DXGI_FORMAT_R32_UINT;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.Width = SwapDesc.BufferDesc.Width;
    TextureDesc.Height = SwapDesc.BufferDesc.Height;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    Device->CreateTexture2D(&TextureDesc, nullptr, &IdBuffer);

    D3D11_RENDER_TARGET_VIEW_DESC Desc{};
    Desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    Desc.Format = DXGI_FORMAT_R32_UINT;
    Device->CreateRenderTargetView(IdBuffer, &Desc, &IdBufferRTV);

    TextureDesc = {};

    TextureDesc.Format = DXGI_FORMAT_R32_UINT;
    TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    TextureDesc.Usage = D3D11_USAGE_STAGING;
    TextureDesc.Width = 1;
    TextureDesc.Height = 1;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.BindFlags = 0;

    Device->CreateTexture2D(&TextureDesc, nullptr, &IdStagingBuffer);
}

void D3D11RHI::CreateDOFResources()
{
    DXGI_SWAP_CHAIN_DESC swapDesc;
    SwapChain->GetDesc(&swapDesc);

    // 1/4 해상도 계산
    UINT quarterWidth = swapDesc.BufferDesc.Width / 4;
    UINT quarterHeight = swapDesc.BufferDesc.Height / 4;

    // DOF 텍스처 Description (고정밀도 Float)
    D3D11_TEXTURE2D_DESC DOFDesc = {};
    DOFDesc.Width = quarterWidth;
    DOFDesc.Height = quarterHeight;
    DOFDesc.MipLevels = 1;
    DOFDesc.ArraySize = 1;
    DOFDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  // RGB=Color, A=CoC
    DOFDesc.SampleDesc.Count = 1;
    DOFDesc.SampleDesc.Quality = 0;
    DOFDesc.Usage = D3D11_USAGE_DEFAULT;
    DOFDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    DOFDesc.CPUAccessFlags = 0;
    DOFDesc.MiscFlags = 0;

    // 4개 DOF 버퍼 생성
    for (uint32 Idx = 0; Idx < NUM_DOF_BUFFERS; ++Idx)
    {
        HRESULT Result = Device->CreateTexture2D(&DOFDesc, nullptr, &DOFTextures[Idx]);
        if (FAILED(Result))
        {
            UE_LOG("D3D11RHI: DOF Texture[%d] 생성 실패\n", Idx);
            return;
        }

        // RTV 생성
        D3D11_RENDER_TARGET_VIEW_DESC RtvDesc = {};
        RtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        RtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        RtvDesc.Texture2D.MipSlice = 0;

        Result = Device->CreateRenderTargetView(DOFTextures[Idx], &RtvDesc, &DOFRTVs[Idx]);
        if (FAILED(Result))
        {
            UE_LOG("D3D11RHI: DOF RTV[%d] 생성 실패\n", Idx);
            return;
        }

        // SRV 생성
        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels = 1;

        Result = Device->CreateShaderResourceView(DOFTextures[Idx], &SRVDesc, &DOFSRVs[Idx]);
        if (FAILED(Result))
        {
            UE_LOG("D3D11RHI: DOF SRV[%d] 생성 실패\n", Idx);
        }
    }

    UE_LOG("D3D11RHI: DOF Resources 생성 완료 (%d x %d)\n", quarterWidth, quarterHeight);
}

void D3D11RHI::CreateRasterizerState()
{
    D3D11_RASTERIZER_DESC deafultrasterizerdesc = {};
    deafultrasterizerdesc.FillMode = D3D11_FILL_SOLID; // 채우기 모드
    deafultrasterizerdesc.CullMode = D3D11_CULL_BACK; // 백 페이스 컬링
    deafultrasterizerdesc.DepthClipEnable = TRUE; // 근/원거리 평면 클리핑

    Device->CreateRasterizerState(&deafultrasterizerdesc, &DefaultRasterizerState);

    D3D11_RASTERIZER_DESC wireframerasterizerdesc = {};
    wireframerasterizerdesc.FillMode = D3D11_FILL_WIREFRAME; // 채우기 모드
    wireframerasterizerdesc.CullMode = D3D11_CULL_BACK; // 백 페이스 컬링
    wireframerasterizerdesc.DepthClipEnable = TRUE; // 근/원거리 평면 클리핑

    Device->CreateRasterizerState(&wireframerasterizerdesc, &WireFrameRasterizerState);

    D3D11_RASTERIZER_DESC nocullRasterizerDesc = {};
    nocullRasterizerDesc.FillMode = D3D11_FILL_SOLID;
    nocullRasterizerDesc.CullMode = D3D11_CULL_NONE;
    nocullRasterizerDesc.DepthClipEnable = TRUE;

    Device->CreateRasterizerState(&nocullRasterizerDesc, &NoCullRasterizerState);

    // Z-Fighting 방지를 위한 데칼 전용 래스터라이저 상태 생성
    D3D11_RASTERIZER_DESC DecalRasterizerDesc = {};
    DecalRasterizerDesc.FillMode = D3D11_FILL_SOLID;
    DecalRasterizerDesc.CullMode = D3D11_CULL_BACK; // 데칼 메시에 따라 D3D11_CULL_NONE이 필요할 수 있습니다.
    DecalRasterizerDesc.DepthClipEnable = TRUE;

    // Z-Fighting 해결을 위한 핵심 설정
    // 이 값들은 장면에 따라 조정이 필요할 수 있는 시작 값입니다.
    DecalRasterizerDesc.DepthBias = -100; // 깊이 버퍼의 최소 단위만큼 밀어냅니다. (음수 = 카메라 쪽으로)
    DecalRasterizerDesc.SlopeScaledDepthBias = -1.0f; // 폴리곤의 기울기에 비례하여 바이어스를 적용합니다.
    DecalRasterizerDesc.DepthBiasClamp = 0.0f; // 바이어스 최댓값 (0.0f는 제한 없음)

    Device->CreateRasterizerState(&DecalRasterizerDesc, &DecalRasterizerState);

    // 섀도우 맵 전용 래스터라이저
    D3D11_RASTERIZER_DESC ShadowRasterizerDesc = {};
    ShadowRasterizerDesc.FillMode = D3D11_FILL_SOLID;
    ShadowRasterizerDesc.CullMode = D3D11_CULL_NONE; // 또는 CULL_FRONT (섀도우 기법에 따라 다름)
    ShadowRasterizerDesc.DepthClipEnable = TRUE;

    // 섀도우 아티팩트(Acne) 방지를 위한 Bias 설정
    ShadowRasterizerDesc.DepthBias = 1000;              // (값은 튜닝 필요)
    ShadowRasterizerDesc.SlopeScaledDepthBias = 4.0f;   // (값은 튜닝 필요)
    ShadowRasterizerDesc.DepthBiasClamp = 0.0f;

    Device->CreateRasterizerState(&ShadowRasterizerDesc, &ShadowRasterizerState);
}

void D3D11RHI::CreateConstantBuffer(ID3D11Buffer** ConstantBuffer, uint32 Size)
{
    D3D11_BUFFER_DESC BufferDesc{};
    BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    BufferDesc.ByteWidth = (Size+15) & ~15;
    BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Device->CreateBuffer(&BufferDesc, nullptr, ConstantBuffer);
}

void D3D11RHI::UpdateUVScrollConstantBuffers(const FVector2D& Speed, float TimeSec)
{
    if (!UVScrollCB) return;

    struct { float x; float y; float t; float pad; } data { Speed.X, Speed.Y, TimeSec, 0.0f };

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(DeviceContext->Map(UVScrollCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &data, sizeof(data));
        DeviceContext->Unmap(UVScrollCB, 0);
        DeviceContext->PSSetConstantBuffers(5, 1, &UVScrollCB);
    }
}

void D3D11RHI::ReleaseSamplerState()
{
    if (DefaultSamplerState)
    {
        DefaultSamplerState->Release();
        DefaultSamplerState = nullptr;
	}
    if (LinearClampSamplerState)
    {
        LinearClampSamplerState->Release();
        LinearClampSamplerState = nullptr;
    }
    if (PointClampSamplerState)
    {
        PointClampSamplerState->Release();
        PointClampSamplerState = nullptr;
    }
    if (ShadowSamplerState)
    {
        ShadowSamplerState->Release();
        ShadowSamplerState = nullptr;
    }
    if (VSMSamplerState)
    {
        VSMSamplerState->Release();
        VSMSamplerState = nullptr;
    }
}

void D3D11RHI::ReleaseBlendState()
{
    if (BlendStateOpaque)
    {
        BlendStateOpaque->Release();
        BlendStateOpaque = nullptr;
    }
    if (BlendStateTransparent)
    {
        BlendStateTransparent->Release();
        BlendStateTransparent = nullptr;
    }
}

void D3D11RHI::ReleaseRasterizerState()
{
    if (DefaultRasterizerState)
    {
        DefaultRasterizerState->Release();
        DefaultRasterizerState = nullptr;
    }
    if (WireFrameRasterizerState)
    {
        WireFrameRasterizerState->Release();
        WireFrameRasterizerState = nullptr;
    }
    if (DecalRasterizerState)
    {
        DecalRasterizerState->Release();
        DecalRasterizerState = nullptr;
    }
    if (ShadowRasterizerState)
    {
        ShadowRasterizerState->Release();
        ShadowRasterizerState = nullptr;
    }
    if (NoCullRasterizerState)
    {
        NoCullRasterizerState->Release();
        NoCullRasterizerState = nullptr;
    }
    DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}

void D3D11RHI::ReleaseFrameBuffer()
{
    // BackBufferRTV를 먼저 해제 (View가 Texture보다 먼저)
    if (BackBufferRTV)
    {
        BackBufferRTV->Release();
        BackBufferRTV = nullptr;
    }

    // FrameBuffer는 SwapChain에서 GetBuffer()로 가져온 것이므로 Release 필요
    if (FrameBuffer)
    {
        FrameBuffer->Release();
        FrameBuffer = nullptr;
    }

    // 모든 View를 먼저 해제 (중요: View -> Texture 순서)

    // DepthStencilView 해제
    if (DepthStencilView)
    {
        DepthStencilView->Release();
        DepthStencilView = nullptr;
    }

    // Depth SRV 해제
    if (DepthSRV)
    {
        DepthSRV->Release();
        DepthSRV = nullptr;
    }

    // 모든 View를 해제한 후 Texture 해제
    if (DepthBuffer)
    {
        DepthBuffer->Release();
        DepthBuffer = nullptr;
    }

    // 해제 순서 중요 (for문 내부 순서 변경하지 마세요)
    for (int i = 0; i < NUM_SCENE_BUFFERS; i++)
    {
        // 1. RTV
        if (SceneColorRTVs[i])
        {
            SceneColorRTVs[i]->Release();
            SceneColorRTVs[i] = nullptr;
        }

        // 2. RSV
        if (SceneColorSRVs[i])
        {
            SceneColorSRVs[i]->Release();
            SceneColorSRVs[i] = nullptr;
        }

        // 3. Texture
        if (SceneColorTextures[i])
        {
            SceneColorTextures[i]->Release();
            SceneColorTextures[i] = nullptr;
        }
    }
}

void D3D11RHI::ReleaseIdBuffer()
{
    if (IdBufferRTV)
    {
        IdBufferRTV->Release();
        IdBufferRTV = nullptr;
    }
    if (IdStagingBuffer)
    {
        IdStagingBuffer->Release();
        IdStagingBuffer = nullptr;
    }
    if (IdBuffer)
    {
        IdBuffer->Release();
        IdBuffer = nullptr;
    }
}

void D3D11RHI::ReleaseDOFResources()
{
    for (uint32 Idx = 0; Idx < NUM_DOF_BUFFERS; ++Idx)
    {
        // SRV 먼저 해제
        if (DOFSRVs[Idx])
        {
            DOFSRVs[Idx]->Release();
            DOFSRVs[Idx] = nullptr;
        }

        // RTV 해제
        if (DOFRTVs[Idx])
        {
            DOFRTVs[Idx]->Release();
            DOFRTVs[Idx] = nullptr;
        }

        // Texture 해제
        if (DOFTextures[Idx])
        {
            DOFTextures[Idx]->Release();
            DOFTextures[Idx] = nullptr;
        }
    }
}

void D3D11RHI::ReleaseDeviceAndSwapChain()
{
    if (SwapChain)
    {
        SwapChain->Release();
        SwapChain = nullptr;
    }

    if (DeviceContext)
    {
        DeviceContext->Release();
        DeviceContext = nullptr;
    }

    // VRAM Leak 디버깅용 함수
    // DXGI 누출 로그 출력 시 주석 해제로 타입 확인 가능
    // ID3D11Debug* DebugPointer = nullptr;
    // HRESULT Result = GetDevice()->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&DebugPointer));
    // if (SUCCEEDED(Result))
    // {
    // 	DebugPointer->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
    // 	DebugPointer->Release();
    // }
    
    if (Device)
    {
        Device->Release();
        Device = nullptr;
    }
}

void D3D11RHI::OMSetDepthStencilState(EComparisonFunc Func)
{
    switch (Func)
    {
    case EComparisonFunc::Always:
        DeviceContext->OMSetDepthStencilState(DepthStencilStateAlwaysNoWrite, 0);
        break;
    case EComparisonFunc::LessEqual:
        DeviceContext->OMSetDepthStencilState(DepthStencilStateLessEqualWrite, 0);
        break;
    case EComparisonFunc::GreaterEqual:
        DeviceContext->OMSetDepthStencilState(DepthStencilStateGreaterEqualWrite, 0);
        break;
    case EComparisonFunc::LessEqualReadOnly:
        DeviceContext->OMSetDepthStencilState(DepthStencilStateLessEqualReadOnly, 0);
        break;
    }
}

void D3D11RHI::OMSetDepthStencilState_OverlayWriteStencil()
{
    // Stencil ref = 1 (overlay marks)
    DeviceContext->OMSetDepthStencilState(DepthStencilStateOverlayWriteStencil, 1);
}

void D3D11RHI::OMSetDepthStencilState_StencilRejectOverlay()
{
    // Stencil ref = 0 (draw only where overlay not marked)
    DeviceContext->OMSetDepthStencilState(DepthStencilStateStencilRejectOverlay, 0);
}

void D3D11RHI::CreateShader(ID3D11InputLayout** SimpleInputLayout, ID3D11VertexShader** SimpleVertexShader, ID3D11PixelShader** SimplePixelShader)
{
    ID3DBlob* vertexshaderCSO;
    ID3DBlob* pixelshaderCSO;

    D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainVS", "vs_5_0", 0, 0, &vertexshaderCSO, nullptr);

    Device->CreateVertexShader(vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), nullptr, SimpleVertexShader);

    D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainPS", "ps_5_0", 0, 0, &pixelshaderCSO, nullptr);

    Device->CreatePixelShader(pixelshaderCSO->GetBufferPointer(), pixelshaderCSO->GetBufferSize(), nullptr, SimplePixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    Device->CreateInputLayout(layout, ARRAYSIZE(layout), vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), SimpleInputLayout);

    vertexshaderCSO->Release();
    pixelshaderCSO->Release();
}
void D3D11RHI::OnResize(UINT NewWidth, UINT NewHeight)
{
    if (!Device || !DeviceContext || !SwapChain)
        return;

    if (!SwapChain) return;

    // 렌더링 완료까지 대기 (중요!)
    if (DeviceContext) {
        DeviceContext->Flush();
    }

    // 현재 렌더 타겟 언바인딩
    if (DeviceContext) {
        DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    }


    // 기존 리소스 해제
    ReleaseFrameBuffer();
    ReleaseIdBuffer();
    ReleaseDOFResources();  // DOF 렌더 타겟 해제

    // 스왑체인 버퍼 리사이즈
    HRESULT hr = SwapChain->ResizeBuffers(
        0,                 // 버퍼 개수 (0 = 기존 유지)
        NewWidth,
        NewHeight,
        DXGI_FORMAT_UNKNOWN, // 기존 포맷 유지
        0
    );
    if (FAILED(hr))
    {
        UE_LOG("SwapChain->ResizeBuffers failed!\n");
        return;
    }

    // 새 프레임버퍼/RTV/DSV 생성
    CreateFrameBuffer();
    CreateIdBuffer();
    CreateDOFResources();  // DOF 렌더 타겟 재생성

    // 뷰포트 갱신
    ViewportInfo.TopLeftX = 0.0f;
    ViewportInfo.TopLeftY = 0.0f;
    ViewportInfo.Width = static_cast<float>(NewWidth);
    ViewportInfo.Height = static_cast<float>(NewHeight);
    ViewportInfo.MinDepth = 0.0f;
    ViewportInfo.MaxDepth = 1.0f;

    DeviceContext->RSSetViewports(1, &ViewportInfo);
}
void D3D11RHI::CreateBackBufferAndDepthStencil(UINT width, UINT height)
{
    // 기존 바인딩 해제 후 뷰 해제
    if (BackBufferRTV) { DeviceContext->OMSetRenderTargets(0, nullptr, nullptr); BackBufferRTV->Release(); BackBufferRTV = nullptr; }
    if (DepthStencilView) { DepthStencilView->Release(); DepthStencilView = nullptr; }

    // 1) 백버퍼에서 RTV 생성
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr) || !backBuffer) {
        UE_LOG("GetBuffer(0) failed.\n");
        return;
    }

    D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
    framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = Device->CreateRenderTargetView(backBuffer, &framebufferRTVdesc, &BackBufferRTV);
    backBuffer->Release();
    if (FAILED(hr) || !BackBufferRTV) {
        UE_LOG("CreateRenderTargetView failed.\n");
        return;
    }

    // 2) DepthStencil 텍스처/뷰 생성
    ID3D11Texture2D* depthTex = nullptr;
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;               // 멀티샘플링 끄는 경우
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = Device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    if (FAILED(hr) || !depthTex) {
        UE_LOG("CreateTexture2D(depth) failed.\n");
        return;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = depthDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = Device->CreateDepthStencilView(depthTex, &dsvDesc, &DepthStencilView);
    depthTex->Release();
    if (FAILED(hr) || !DepthStencilView) {
        UE_LOG("CreateDepthStencilView failed.\n");
        return;
    }

    // 3) OM 바인딩
    DeviceContext->OMSetRenderTargets(1, &BackBufferRTV, DepthStencilView);

    // 4) 뷰포트 갱신
    SetViewport(width, height);
}

// ──────────────────────────────────────────────────────
// Helper: Viewport 갱신
// ──────────────────────────────────────────────────────
void D3D11RHI::SetViewport(UINT width, UINT height)
{
    ViewportInfo.TopLeftX = 0.0f;
    ViewportInfo.TopLeftY = 0.0f;
    ViewportInfo.Width = static_cast<float>(width);
    ViewportInfo.Height = static_cast<float>(height);
    ViewportInfo.MinDepth = 0.0f;
    ViewportInfo.MaxDepth = 1.0f;

    DeviceContext->RSSetViewports(1, &ViewportInfo);
}

void D3D11RHI::PSSetDefaultSampler(UINT StartSlot)
{
	DeviceContext->PSSetSamplers(StartSlot, 1, &DefaultSamplerState);
}

void D3D11RHI::PSSetClampSampler(UINT StartSlot)
{
    DeviceContext->PSSetSamplers(StartSlot, 1, &LinearClampSamplerState);
}

ID3D11SamplerState* D3D11RHI::GetSamplerState(RHI_Sampler_Index SamplerIndex) const
{
	ID3D11SamplerState* TempSamplerState = nullptr;
    switch (SamplerIndex)
    {
    case RHI_Sampler_Index::Default:
        TempSamplerState = DefaultSamplerState;
        break;
    case RHI_Sampler_Index::LinearClamp:
        TempSamplerState = LinearClampSamplerState;
        break;
    case RHI_Sampler_Index::PointClamp:
		TempSamplerState = PointClampSamplerState;
        break;
    case RHI_Sampler_Index::Shadow:
        TempSamplerState = ShadowSamplerState;
        break;
    case RHI_Sampler_Index::VSM:
        TempSamplerState = VSMSamplerState;
        break;
    default:
        TempSamplerState = nullptr;
        break;
    }
    return TempSamplerState;
}


void D3D11RHI::PrepareShader(UShader* InShader)
{
    GetDeviceContext()->VSSetShader(InShader->GetVertexShader(), nullptr, 0);
    GetDeviceContext()->PSSetShader(InShader->GetPixelShader(), nullptr, 0);
    GetDeviceContext()->IASetInputLayout(InShader->GetInputLayout());
}

void D3D11RHI::PrepareShader(UShader* InVertexShader, UShader* InPixelShader)
{
    GetDeviceContext()->IASetInputLayout(InVertexShader->GetInputLayout());
    GetDeviceContext()->VSSetShader(InVertexShader->GetVertexShader(), nullptr, 0);

    GetDeviceContext()->PSSetShader(InPixelShader->GetPixelShader(), nullptr, 0);
}

// ──────────────────────────────────────────────────────
// Structured Buffer 관련 메서드 (타일 기반 라이트 컬링용)
// ──────────────────────────────────────────────────────

HRESULT D3D11RHI::CreateStructuredBuffer(UINT InElementSize, UINT InElementCount, const void* InInitData, ID3D11Buffer** OutBuffer)
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;  // CPU에서 업데이트 가능
    bufferDesc.ByteWidth = InElementSize * InElementCount;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = InElementSize;

    if (InInitData)
    {
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = InInitData;
        return Device->CreateBuffer(&bufferDesc, &initData, OutBuffer);
    }
    else
    {
        return Device->CreateBuffer(&bufferDesc, nullptr, OutBuffer);
    }
}

HRESULT D3D11RHI::CreateStructuredBufferSRV(ID3D11Buffer* InBuffer, ID3D11ShaderResourceView** OutSRV)
{
    if (!InBuffer)
        return E_INVALIDARG;

    D3D11_BUFFER_DESC bufferDesc;
    InBuffer->GetDesc(&bufferDesc);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = bufferDesc.ByteWidth / bufferDesc.StructureByteStride;

    return Device->CreateShaderResourceView(InBuffer, &srvDesc, OutSRV);
}

void D3D11RHI::UpdateStructuredBuffer(ID3D11Buffer* InBuffer, const void* InData, UINT InDataSize)
{
    if (!InBuffer || !InData)
        return;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = DeviceContext->Map(InBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, InData, InDataSize);
        DeviceContext->Unmap(InBuffer, 0);
    }
}
