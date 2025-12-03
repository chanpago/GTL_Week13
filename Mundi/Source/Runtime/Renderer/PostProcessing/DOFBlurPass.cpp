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

    // Near/Far 분리 텍스처
    // DOF[0] = Near 소스, DOF[1] = Far 소스
    // DOF[2] = Near 블러 결과, DOF[3] = Far 블러 결과
    ID3D11ShaderResourceView* nearSourceSRV = RHIDevice->GetDOFSRV(0);
    ID3D11ShaderResourceView* farSourceSRV = RHIDevice->GetDOFSRV(1);

    // ===========================================
    // Pass 1: Near (Foreground) Blur
    // DOF[0] → DOF[2]
    // ===========================================
    {
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);

        ID3D11RenderTargetView* nearBlurRTV = RHIDevice->GetDOFRTV(2);
        RHIDevice->OMSetCustomRenderTargets(1, &nearBlurRTV, nullptr);
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(nearBlurRTV, clearColor);

        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &nearSourceSRV);

        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(0.0f, 0.0f);
        blurCB.BlurRadius = NearBlurRadius;
        blurCB.IsFarField = 0;  // Near
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        RHIDevice->DrawFullScreenQuad();
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // ===========================================
    // Pass 2: Far (Background) Blur
    // DOF[1] → DOF[3]
    // ===========================================
    {
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);

        ID3D11RenderTargetView* farBlurRTV = RHIDevice->GetDOFRTV(3);
        RHIDevice->OMSetCustomRenderTargets(1, &farBlurRTV, nullptr);
        RHIDevice->GetDeviceContext()->ClearRenderTargetView(farBlurRTV, clearColor);

        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &farSourceSRV);

        FDOFBlurBufferType blurCB;
        blurCB.BlurDirection = FVector2D(0.0f, 0.0f);
        blurCB.BlurRadius = FarBlurRadius;
        blurCB.IsFarField = 1;  // Far
        RHIDevice->SetAndUpdateConstantBuffer(blurCB);

        RHIDevice->DrawFullScreenQuad();
        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
    }

    // ===========================================
    // Pass 3 & 4: Denoise
    // 블러 후 소스 텍스처(DOF[0], DOF[1])를 임시 버퍼로 재사용
    // ===========================================
    if (bEnableDenoise)
    {
        RHIDevice->PrepareShader(FullScreenVS, DenoisePS);

        // --- Near Denoise: DOF[2] → DOF[0] → DOF[2] ---
        ID3D11RenderTargetView* nearTempRTV = RHIDevice->GetDOFRTV(0);
        ID3D11ShaderResourceView* nearTempSRV = RHIDevice->GetDOFSRV(0);

        // Pass 3a: DOF[2] → DOF[0]
        {
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
            RHIDevice->OMSetCustomRenderTargets(1, &nearTempRTV, nullptr);
            RHIDevice->GetDeviceContext()->ClearRenderTargetView(nearTempRTV, clearColor);

            ID3D11ShaderResourceView* nearBlurSRV = RHIDevice->GetDOFSRV(2);
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &nearBlurSRV);

            RHIDevice->DrawFullScreenQuad();
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
        }

        // Pass 3b: DOF[0] → DOF[2]
        {
            ID3D11RenderTargetView* nearBlurRTV = RHIDevice->GetDOFRTV(2);
            RHIDevice->OMSetCustomRenderTargets(1, &nearBlurRTV, nullptr);

            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &nearTempSRV);

            RHIDevice->DrawFullScreenQuad();
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
        }

        // --- Far Denoise: DOF[3] → DOF[1] → DOF[3] ---
        ID3D11RenderTargetView* farTempRTV = RHIDevice->GetDOFRTV(1);
        ID3D11ShaderResourceView* farTempSRV = RHIDevice->GetDOFSRV(1);

        // Pass 4a: DOF[3] → DOF[1]
        {
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
            RHIDevice->OMSetCustomRenderTargets(1, &farTempRTV, nullptr);
            RHIDevice->GetDeviceContext()->ClearRenderTargetView(farTempRTV, clearColor);

            ID3D11ShaderResourceView* farBlurSRV = RHIDevice->GetDOFSRV(3);
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &farBlurSRV);

            RHIDevice->DrawFullScreenQuad();
            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &NullSRV);
        }

        // Pass 4b: DOF[1] → DOF[3]
        {
            ID3D11RenderTargetView* farBlurRTV = RHIDevice->GetDOFRTV(3);
            RHIDevice->OMSetCustomRenderTargets(1, &farBlurRTV, nullptr);

            RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &farTempSRV);

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
