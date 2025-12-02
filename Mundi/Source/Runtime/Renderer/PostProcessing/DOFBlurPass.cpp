#include "pch.h"
#include "DOFBlurPass.h"
#include "../SceneView.h"
#include "../../RHI/SwapGuard.h"
#include "../../RHI/ConstantBufferType.h"
#include "../../RHI/D3D11RHI.h"

void FDOFBlurPass::Execute(const FPostProcessModifier& M, FSceneView* View, D3D11RHI* RHIDevice)
{
    if (!IsApplicable(M)) return;

    // Viewport를 ViewRect/2 기준으로 설정 (게임 영역만)
    D3D11_VIEWPORT halfViewport;
    halfViewport.TopLeftX = View->ViewRect.MinX / 2.0f;
    halfViewport.TopLeftY = View->ViewRect.MinY / 2.0f;
    halfViewport.Width = View->ViewRect.Width() / 2.0f;
    halfViewport.Height = View->ViewRect.Height() / 2.0f;
    halfViewport.MinDepth = 0.0f;
    halfViewport.MaxDepth = 1.0f;
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &halfViewport);

    // 셰이더 로드
    UShader* FullScreenVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
    UShader* BlurPS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/DOF_Blur_PS.hlsl");
    UShader* DenoisePS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/DOF_Denoise_PS.hlsl");

    if (!FullScreenVS || !BlurPS || !FullScreenVS->GetVertexShader() || !BlurPS->GetPixelShader())
    {
        UE_LOG("DOFBlur: 셰이더 로드 실패!\n");
        return;
    }

    // 디노이즈 셰이더는 선택적 (없으면 디노이즈 스킵)
    bool bEnableDenoise = DenoisePS && DenoisePS->GetPixelShader();

    RHIDevice->PrepareShader(FullScreenVS, BlurPS);

    // Sampler
    ID3D11SamplerState* LinearClamp = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
    ID3D11SamplerState* PointClamp = RHIDevice->GetSamplerState(RHI_Sampler_Index::PointClamp);
    if (!LinearClamp || !PointClamp)
    {
        UE_LOG("DOFBlur: Sampler 없음!\n");
        return;
    }
    ID3D11SamplerState* samplers[2] = { LinearClamp, PointClamp };
    RHIDevice->GetDeviceContext()->PSSetSamplers(0, 2, samplers);

    // Tile SRV 바인딩 (t1)
    if (TileSRV)
    {
        RHIDevice->GetDeviceContext()->PSSetShaderResources(1, 1, &TileSRV);
    }

    // Depth/Blend State
    RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
    RHIDevice->OMSetBlendState(false);

    // Viewport Constants (b10)
    // ScreenSize = DOF 텍스처 크기 (SwapChain/2)
    // ViewportRect = 유효 렌더링 영역 (Viewport/2)
    UINT dofTexWidth = RHIDevice->GetSwapChainWidth() / 2;
    UINT dofTexHeight = RHIDevice->GetSwapChainHeight() / 2;

    FViewportConstants ViewportCB;
    ViewportCB.ViewportRect = FVector4(
        View->ViewRect.MinX / 2.0f,
        View->ViewRect.MinY / 2.0f,
        View->ViewRect.Width() / 2.0f,
        View->ViewRect.Height() / 2.0f
    );
    ViewportCB.ScreenSize = FVector4(
        static_cast<float>(dofTexWidth),
        static_cast<float>(dofTexHeight),
        1.0f / static_cast<float>(dofTexWidth),
        1.0f / static_cast<float>(dofTexHeight)
);
    RHIDevice->SetAndUpdateConstantBuffer(ViewportCB);

    // Blur Radius
    float NearBlurRadius = M.Payload.Params1.Z;  // MaxNearBlurSize
    float FarBlurRadius = M.Payload.Params1.W;   // MaxFarBlurSize

    ID3D11ShaderResourceView* NullSRV = nullptr;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Input SRV: DOF[0] (혼합 텍스처, CoC 부호 있음)
    ID3D11ShaderResourceView* inputSRV = RHIDevice->GetDOFSRV(0);

    // ===========================================
    // Pass 1: Near (Foreground) Blur
    // DOF[0] → DOF[1], IsFarField = 0 (CoC < 0 만 포함)
    // ===========================================
    {
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);

        ID3D11RenderTargetView* nearRTV = RHIDevice->GetDOFRTV(1);
        RHIDevice->OMSetCustomRenderTargets(1, &nearRTV, nullptr);
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(nearRTV, clearColor);

        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &inputSRV);

        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(0.0f, 0.0f);
        blurCB.BlurRadius = NearBlurRadius;  // Near용
        blurCB.IsFarField = 0;  // Near (Foreground)
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        RHIDevice->DrawFullScreenQuad();
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // ===========================================
    // Pass 2: Far (Background) Blur
    // DOF[0] → DOF[2], IsFarField = 1 (CoC >= 0 만 포함)
    // ===========================================
    {
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);

        ID3D11RenderTargetView* farRTV = RHIDevice->GetDOFRTV(2);
        RHIDevice->OMSetCustomRenderTargets(1, &farRTV, nullptr);
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(farRTV, clearColor);

        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &inputSRV);

        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(0.0f, 0.0f);
        blurCB.BlurRadius = FarBlurRadius;  // Far용
        blurCB.IsFarField = 1;  // Far (Background)
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        RHIDevice->DrawFullScreenQuad();
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // Tile SRV 언바인드
    if (TileSRV)
    {
        RHIDevice->GetDeviceContext()->PSSetShaderResources(1, 1, &NullSRV);
    }

    // ===========================================
    // Pass 3 & 4: Denoise (지터링 노이즈 제거)
    // DOF[3]을 임시 버퍼로 사용하는 Ping-Pong 방식
    // ===========================================
    if (bEnableDenoise)
    {
        RHIDevice->PrepareShader(FullScreenVS, DenoisePS);

        ID3D11RenderTargetView* tempRTV = RHIDevice->GetDOFRTV(3);
        ID3D11ShaderResourceView* tempSRV = RHIDevice->GetDOFSRV(3);

        // --- Near Denoise: DOF[1] → DOF[3] → DOF[1] ---
        // Pass 3a: DOF[1] → DOF[3]
        {
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
            RHIDevice->OMSetCustomRenderTargets(1, &tempRTV, nullptr);
            RHIDevice->GetDeviceContext()->ClearRenderTargetView(tempRTV, clearColor);

            ID3D11ShaderResourceView* nearSRV = RHIDevice->GetDOFSRV(1);
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &nearSRV);

            RHIDevice->DrawFullScreenQuad();
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
        }

        // Pass 3b: DOF[3] → DOF[1]
        {
            ID3D11RenderTargetView* nearRTV = RHIDevice->GetDOFRTV(1);
            RHIDevice->OMSetCustomRenderTargets(1, &nearRTV, nullptr);

            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &tempSRV);

            RHIDevice->DrawFullScreenQuad();
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
        }

        // --- Far Denoise: DOF[2] → DOF[3] → DOF[2] ---
        // Pass 4a: DOF[2] → DOF[3]
        {
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
            RHIDevice->OMSetCustomRenderTargets(1, &tempRTV, nullptr);
            RHIDevice->GetDeviceContext()->ClearRenderTargetView(tempRTV, clearColor);

            ID3D11ShaderResourceView* farSRV = RHIDevice->GetDOFSRV(2);
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &farSRV);

            RHIDevice->DrawFullScreenQuad();
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
        }

        // Pass 4b: DOF[3] → DOF[2]
        {
            ID3D11RenderTargetView* farRTV = RHIDevice->GetDOFRTV(2);
            RHIDevice->OMSetCustomRenderTargets(1, &farRTV, nullptr);

            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &tempSRV);

            RHIDevice->DrawFullScreenQuad();
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
        }
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
