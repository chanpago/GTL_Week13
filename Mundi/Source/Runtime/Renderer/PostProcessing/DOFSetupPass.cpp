#include "pch.h"
#include "DOFSetupPass.h"
#include "../SceneView.h"
#include "../../RHI/SwapGuard.h"
#include "../../RHI/ConstantBufferType.h"
#include "../../RHI/D3D11RHI.h"

void FDOFSetupPass::Execute(const FPostProcessModifier& M, FSceneView* View, D3D11RHI* RHIDevice)
{
    if (!IsApplicable(M)) return;

    // 1) 스왑 + SRV 언바인드 (SceneColorSource를 읽기 위해)
    //    DOFSetupPass는 DOFRTVs에 쓰므로 Commit() 안 함 → 소멸자에서 스왑 복원
    FSwapGuard SwapGuard(RHIDevice, 0, 2);

    // 2) SRV 언바인드 (SceneColor + SceneDepth = 2개)
    ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
    RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 2, NullSRVs);

    // 3) MRT 설정: Far Field (RTV[0]), Near Field (RTV[1])
    ID3D11RenderTargetView** DOFRTVs = RHIDevice->GetDOFRTVs();

    RHIDevice->OMSetCustomRenderTargets(2, DOFRTVs, nullptr);

    // 2-1) Viewport를 ViewRect/4 기준으로 설정 (게임 영역만)
    D3D11_VIEWPORT quarterViewport;
    quarterViewport.TopLeftX = View->ViewRect.MinX / 4.0f;
    quarterViewport.TopLeftY = View->ViewRect.MinY / 4.0f;
    quarterViewport.Width = View->ViewRect.Width() / 4.0f;
    quarterViewport.Height = View->ViewRect.Height() / 4.0f;
    quarterViewport.MinDepth = 0.0f;
    quarterViewport.MaxDepth = 1.0f;
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &quarterViewport);

    // 3) Clear RTV (검은색)
    float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    RHIDevice->GetDeviceContext()->ClearRenderTargetView(DOFRTVs[0], ClearColor);
    RHIDevice->GetDeviceContext()->ClearRenderTargetView(DOFRTVs[1], ClearColor);

    // 4) Depth State: Depth Test/Write OFF
    RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
    RHIDevice->OMSetBlendState(false);

    // 5) 셰이더 로드
    UShader* FullScreenVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
    UShader* SetupPS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/DOF_Setup_PS.hlsl");

    if (!FullScreenVS || !SetupPS || !FullScreenVS->GetVertexShader() || !SetupPS->GetPixelShader())
    {
        UE_LOG("DOFSetup: 셰이더 로드 실패!\n");
        return;
    }

    RHIDevice->PrepareShader(FullScreenVS, SetupPS);

    // 6) SRV 바인딩 (t0: SceneColor, t1: SceneDepth)
    ID3D11ShaderResourceView* SceneSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneColorSource);
    ID3D11ShaderResourceView* DepthSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneDepth);
    ID3D11SamplerState* LinearClamp = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
    ID3D11SamplerState* PointClamp = RHIDevice->GetSamplerState(RHI_Sampler_Index::PointClamp);

    if (!SceneSRV || !DepthSRV || !LinearClamp || !PointClamp)
    {
        UE_LOG("DOFSetup: SRV/Sampler 없음!\n");
        return;
    }

    ID3D11ShaderResourceView* InputSRVs[2] = { SceneSRV, DepthSRV };
    RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 2, InputSRVs);

    ID3D11SamplerState* Samplers[2] = { LinearClamp, PointClamp };
    RHIDevice->GetDeviceContext()->PSSetSamplers(0, 2, Samplers);

    // 7) 상수 버퍼 업데이트
    // b0: PostProcessCB
    PostProcessBufferType PostProcessCB;
    PostProcessCB.Near = View->NearClip;
    PostProcessCB.Far = View->FarClip;
    PostProcessCB.IsOrthographic = (View->ProjectionMode == ECameraProjectionMode::Orthographic) ? 1 : 0;
    PostProcessCB.Padding = 0.0f;
    RHIDevice->SetAndUpdateConstantBuffer(PostProcessCB);

    // b1: ViewProjBuffer (이미 설정되어 있을 수도 있지만 확실하게)
    ViewProjBufferType ViewProjCB;
    ViewProjCB.View = View->ViewMatrix;
    ViewProjCB.Proj = View->ProjectionMatrix;
    ViewProjCB.InvView = View->ViewMatrix.Inverse();
    ViewProjCB.InvProj = View->ProjectionMatrix.Inverse();
    RHIDevice->SetAndUpdateConstantBuffer(ViewProjCB);

    // b2: DOFSetupCB
    FDOFSetupBufferType DOFSetupCB;
    DOFSetupCB.FocalDistance = M.Payload.Params0.X;
    DOFSetupCB.FocalRegion = M.Payload.Params0.W;
    DOFSetupCB.NearTransitionRegion = M.Payload.Params1.X;
    DOFSetupCB.FarTransitionRegion = M.Payload.Params1.Y;

    DOFSetupCB.MaxNearBlurSize = M.Payload.Params1.Z;
    DOFSetupCB.MaxFarBlurSize = M.Payload.Params1.W;
    DOFSetupCB.NearClip = View->NearClip;
    DOFSetupCB.FarClip = View->FarClip;

    DOFSetupCB.IsOrthographic = (View->ProjectionMode == ECameraProjectionMode::Orthographic) ? 1 : 0;
    DOFSetupCB._Pad0 = FVector::Zero();

    RHIDevice->SetAndUpdateConstantBuffer(DOFSetupCB);

    // b10: ViewportConstants
    // ScreenSize = SceneColor 텍스처 크기 (SwapChain)
    // ViewportRect = 유효 렌더링 영역 (현재 Viewport)
    UINT sceneTexWidth = RHIDevice->GetSwapChainWidth();
    UINT sceneTexHeight = RHIDevice->GetSwapChainHeight();

    FViewportConstants ViewportCB;
    ViewportCB.ViewportRect = FVector4(
        View->ViewRect.MinX,
        View->ViewRect.MinY,
        View->ViewRect.Width(),
        View->ViewRect.Height()
    );
    ViewportCB.ScreenSize = FVector4(
        static_cast<float>(sceneTexWidth),
        static_cast<float>(sceneTexHeight),
        1.0f / static_cast<float>(sceneTexWidth),
        1.0f / static_cast<float>(sceneTexHeight)
    );
    RHIDevice->SetAndUpdateConstantBuffer(ViewportCB);

    // 8) Draw
    RHIDevice->DrawFullScreenQuad();

    // 9) SRV 언바인드
    RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 2, NullSRVs);

    // 10) Viewport를 전체 해상도로 복원
    D3D11_VIEWPORT fullViewport;
    fullViewport.TopLeftX = 0.0f;
    fullViewport.TopLeftY = 0.0f;
    fullViewport.Width = static_cast<float>(RHIDevice->GetViewportWidth());
    fullViewport.Height = static_cast<float>(RHIDevice->GetViewportHeight());
    fullViewport.MinDepth = 0.0f;
    fullViewport.MaxDepth = 1.0f;
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &fullViewport);

    // 11) RTV를 SceneColor로 복원
    RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);
}
