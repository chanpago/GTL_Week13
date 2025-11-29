#include "pch.h"
#include "DOFRecombinePass.h"
#include "../SceneView.h"
#include "../../RHI/SwapGuard.h"
#include "../../RHI/ConstantBufferType.h"
#include "../../RHI/D3D11RHI.h"

void FDOFRecombinePass::Execute(const FPostProcessModifier& M, FSceneView* View, D3D11RHI* RHIDevice)
{
    if (!IsApplicable(M)) return;

    // FSwapGuard: SceneColor 읽기/쓰기 자동 처리 + SRV 0~3 자동 언바인드
    FSwapGuard SwapGuard(RHIDevice, 0, 4);

    // 1) RTV 설정: SceneColor Target
    RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);

    // 2) Depth State: Depth Test/Write OFF
    RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
    RHIDevice->OMSetBlendState(false);

    // 3) 셰이더 로드
    UShader* FullScreenVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
    UShader* RecombinePS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/DOF_Recombine_PS.hlsl");

    if (!FullScreenVS || !RecombinePS || !FullScreenVS->GetVertexShader() || !RecombinePS->GetPixelShader())
    {
        UE_LOG("DOFRecombine: 셰이더 로드 실패!\n");
        return;
    }

    RHIDevice->PrepareShader(FullScreenVS, RecombinePS);

    // 4) SRV 바인딩 (t0~t3)
    ID3D11ShaderResourceView* SceneSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneColorSource);
    ID3D11ShaderResourceView* DepthSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneDepth);
    ID3D11ShaderResourceView* FarSRV = RHIDevice->GetSRV(RHI_SRV_Index::DOFFar);
    ID3D11ShaderResourceView* NearSRV = RHIDevice->GetSRV(RHI_SRV_Index::DOFNear);

    if (!SceneSRV || !DepthSRV || !FarSRV || !NearSRV)
    {
        UE_LOG("DOFRecombine: SRV 없음!\n");
        return;
    }

    ID3D11ShaderResourceView* InputSRVs[4] = { SceneSRV, DepthSRV, FarSRV, NearSRV };
    RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 4, InputSRVs);

    // 5) Sampler 바인딩
    ID3D11SamplerState* LinearClamp = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
    ID3D11SamplerState* PointClamp = RHIDevice->GetSamplerState(RHI_Sampler_Index::PointClamp);

    if (!LinearClamp || !PointClamp)
    {
        UE_LOG("DOFRecombine: Sampler 없음!\n");
        return;
    }

    ID3D11SamplerState* Samplers[2] = { LinearClamp, PointClamp };
    RHIDevice->GetDeviceContext()->PSSetSamplers(0, 2, Samplers);

    // 6) 상수 버퍼 업데이트
    // b0: PostProcessCB
    PostProcessBufferType PostProcessCB;
    PostProcessCB.Near = View->NearClip;
    PostProcessCB.Far = View->FarClip;
    PostProcessCB.IsOrthographic = (View->ProjectionMode == ECameraProjectionMode::Orthographic) ? 1 : 0;
    PostProcessCB.Padding = 0.0f;
    RHIDevice->SetAndUpdateConstantBuffer(PostProcessCB);

    // b1: ViewProjBuffer
    ViewProjBufferType ViewProjCB;
    ViewProjCB.View = View->ViewMatrix;
    ViewProjCB.Proj = View->ProjectionMatrix;
    ViewProjCB.InvView = View->ViewMatrix.Inverse();
    ViewProjCB.InvProj = View->ProjectionMatrix.Inverse();
    RHIDevice->SetAndUpdateConstantBuffer(ViewProjCB);

    // b2: DOFRecombineCB
    FDOFRecombineBufferType DOFRecombineCB;
    DOFRecombineCB.FocalDistance = M.Payload.Params0.X;
    DOFRecombineCB.FocalRegion = M.Payload.Params0.W;
    DOFRecombineCB.NearTransitionRegion = M.Payload.Params1.X;
    DOFRecombineCB.FarTransitionRegion = M.Payload.Params1.Y;

    DOFRecombineCB.MaxNearBlurSize = M.Payload.Params1.Z;
    DOFRecombineCB.MaxFarBlurSize = M.Payload.Params1.W;
    DOFRecombineCB.NearClip = View->NearClip;
    DOFRecombineCB.FarClip = View->FarClip;

    DOFRecombineCB.IsOrthographic = (View->ProjectionMode == ECameraProjectionMode::Orthographic) ? 1 : 0;
    DOFRecombineCB._Pad0 = 0.0f;

    // ViewRect UV 범위 계산 (게임 영역만 DOF 적용)
    UINT swapChainWidth = RHIDevice->GetSwapChainWidth();
    UINT swapChainHeight = RHIDevice->GetSwapChainHeight();
    float swapW = static_cast<float>(swapChainWidth);
    float swapH = static_cast<float>(swapChainHeight);

    DOFRecombineCB.ViewRectMinUV = FVector2D(
        View->ViewRect.MinX / swapW,
        View->ViewRect.MinY / swapH
    );
    DOFRecombineCB.ViewRectMaxUV = FVector2D(
        (View->ViewRect.MinX + View->ViewRect.Width()) / swapW,
        (View->ViewRect.MinY + View->ViewRect.Height()) / swapH
    );

    RHIDevice->SetAndUpdateConstantBuffer(DOFRecombineCB);

    // b10: ViewportConstants - 전체 화면으로 설정 (texCoord 0~1)
    FViewportConstants ViewportCB;
    ViewportCB.ViewportRect = FVector4(0.0f, 0.0f, swapW, swapH);
    ViewportCB.ScreenSize = FVector4(swapW, swapH, 1.0f / swapW, 1.0f / swapH);
    RHIDevice->SetAndUpdateConstantBuffer(ViewportCB);

    // 7) Draw
    RHIDevice->DrawFullScreenQuad();

    // 8) 스왑 확정 (다음 패스에서 결과 사용 가능)
    SwapGuard.Commit();
}
