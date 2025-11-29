#include "pch.h"
#include "DOFBlurPass.h"
#include "../SceneView.h"
#include "../../RHI/SwapGuard.h"
#include "../../RHI/ConstantBufferType.h"
#include "../../RHI/D3D11RHI.h"

void FDOFBlurPass::Execute(const FPostProcessModifier& M, FSceneView* View, D3D11RHI* RHIDevice)
{
    if (!IsApplicable(M)) return;

    // Viewport를 ViewRect/4 기준으로 설정 (게임 영역만)
    D3D11_VIEWPORT quarterViewport;
    quarterViewport.TopLeftX = View->ViewRect.MinX / 4.0f;
    quarterViewport.TopLeftY = View->ViewRect.MinY / 4.0f;
    quarterViewport.Width = View->ViewRect.Width() / 4.0f;
    quarterViewport.Height = View->ViewRect.Height() / 4.0f;
    quarterViewport.MinDepth = 0.0f;
    quarterViewport.MaxDepth = 1.0f;
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &quarterViewport);

    // 셰이더 로드
    UShader* FullScreenVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
    UShader* BlurPS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/DOF_Blur_PS.hlsl");

    if (!FullScreenVS || !BlurPS || !FullScreenVS->GetVertexShader() || !BlurPS->GetPixelShader())
    {
        UE_LOG("DOFBlur: 셰이더 로드 실패!\n");
        return;
    }

    RHIDevice->PrepareShader(FullScreenVS, BlurPS);

    // Sampler
    ID3D11SamplerState* LinearClamp = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
    if (!LinearClamp)
    {
        UE_LOG("DOFBlur: Sampler 없음!\n");
        return;
    }
    RHIDevice->GetDeviceContext()->PSSetSamplers(0, 1, &LinearClamp);

    // Depth/Blend State
    RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
    RHIDevice->OMSetBlendState(false);

    // Viewport Constants (b10)
    // ScreenSize = DOF 텍스처 크기 (SwapChain/4)
    // ViewportRect = 유효 렌더링 영역 (Viewport/4)
    UINT dofTexWidth = RHIDevice->GetSwapChainWidth() / 4;
    UINT dofTexHeight = RHIDevice->GetSwapChainHeight() / 4;

    FViewportConstants ViewportCB;
    ViewportCB.ViewportRect = FVector4(
        View->ViewRect.MinX / 4.0f,
        View->ViewRect.MinY / 4.0f,
        View->ViewRect.Width() / 4.0f,
        View->ViewRect.Height() / 4.0f
    );
    ViewportCB.ScreenSize = FVector4(
        static_cast<float>(dofTexWidth),
        static_cast<float>(dofTexHeight),
        1.0f / static_cast<float>(dofTexWidth),
        1.0f / static_cast<float>(dofTexHeight)
    );
    RHIDevice->SetAndUpdateConstantBuffer(ViewportCB);

    // Blur Radius (MaxBlurSize from DOF params)
    float BlurRadius = FMath::Max(M.Payload.Params1.Z, M.Payload.Params1.W);  // max(MaxNearBlur, MaxFarBlur)

    ID3D11ShaderResourceView* NullSRV = nullptr;

    // ===========================================
    // 1. Far Field - Horizontal Blur
    // ===========================================
    {
        // SRV 언바인드
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);

        // RTV 설정: DOFTextures[2] (Temp)
        ID3D11RenderTargetView* tempRTV = RHIDevice->GetDOFRTV(2);
        RHIDevice->OMSetCustomRenderTargets(1, &tempRTV, nullptr);

        // Clear
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(tempRTV, clearColor);

        // SRV 바인딩: DOFTextures[0] (Far Field)
        ID3D11ShaderResourceView* farSRV = RHIDevice->GetSRV(RHI_SRV_Index::DOFFar);
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &farSRV);

        // 상수 버퍼: Horizontal
        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(1.0f, 0.0f);
        blurCB.BlurRadius = BlurRadius;
        blurCB._Pad0 = 0.0f;
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        // Draw
        RHIDevice->DrawFullScreenQuad();

        // SRV 언바인드
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // ===========================================
    // 2. Far Field - Vertical Blur
    // ===========================================
    {
        // RTV 설정: DOFTextures[0] (Far Field - 최종)
        ID3D11RenderTargetView* farRTV = RHIDevice->GetDOFRTV(0);
        RHIDevice->OMSetCustomRenderTargets(1, &farRTV, nullptr);

        // Clear
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(farRTV, clearColor);

        // SRV 바인딩: DOFTextures[2] (Temp)
        ID3D11ShaderResourceView* tempSRV = RHIDevice->GetSRV(RHI_SRV_Index::DOFTempH);
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &tempSRV);

        // 상수 버퍼: Vertical
        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(0.0f, 1.0f);
        blurCB.BlurRadius = BlurRadius;
        blurCB._Pad0 = 0.0f;
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        // Draw
        RHIDevice->DrawFullScreenQuad();

        // SRV 언바인드
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // ===========================================
    // 3. Near Field - Horizontal Blur
    // ===========================================
    {
        // RTV 설정: DOFTextures[2] (Temp)
        ID3D11RenderTargetView* tempRTV = RHIDevice->GetDOFRTV(2);
        RHIDevice->OMSetCustomRenderTargets(1, &tempRTV, nullptr);

        // Clear
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(tempRTV, clearColor);

        // SRV 바인딩: DOFTextures[1] (Near Field)
        ID3D11ShaderResourceView* nearSRV = RHIDevice->GetSRV(RHI_SRV_Index::DOFNear);
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &nearSRV);

        // 상수 버퍼: Horizontal
        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(1.0f, 0.0f);
        blurCB.BlurRadius = BlurRadius;
        blurCB._Pad0 = 0.0f;
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        // Draw
        RHIDevice->DrawFullScreenQuad();

        // SRV 언바인드
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // ===========================================
    // 4. Near Field - Vertical Blur
    // ===========================================
    {
        // RTV 설정: DOFTextures[1] (Near Field - 최종)
        ID3D11RenderTargetView* nearRTV = RHIDevice->GetDOFRTV(1);
        RHIDevice->OMSetCustomRenderTargets(1, &nearRTV, nullptr);

        // Clear
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(nearRTV, clearColor);

        // SRV 바인딩: DOFTextures[2] (Temp)
        ID3D11ShaderResourceView* tempSRV = RHIDevice->GetSRV(RHI_SRV_Index::DOFTempH);
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &tempSRV);

        // 상수 버퍼: Vertical
        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(0.0f, 1.0f);
        blurCB.BlurRadius = BlurRadius;
        blurCB._Pad0 = 0.0f;
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        // Draw
        RHIDevice->DrawFullScreenQuad();

        // SRV 언바인드
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // Viewport를 전체 해상도로 복원
    D3D11_VIEWPORT fullViewport;
    fullViewport.TopLeftX = 0.0f;
    fullViewport.TopLeftY = 0.0f;
    fullViewport.Width = static_cast<float>(RHIDevice->GetViewportWidth());
    fullViewport.Height = static_cast<float>(RHIDevice->GetViewportHeight());
    fullViewport.MinDepth = 0.0f;
    fullViewport.MaxDepth = 1.0f;
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &fullViewport);

    // RTV 복원
    RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);
}
