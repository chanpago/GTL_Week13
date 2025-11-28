#pragma once
#include "CameraModifierBase.h"
#include "PostProcessing/PostProcessing.h"

class UCamMod_DOF : public UCameraModifierBase
{
public:
    DECLARE_CLASS(UCamMod_DOF, UCameraModifierBase)

    UCamMod_DOF() = default;
    virtual ~UCamMod_DOF() = default;

    // DOF 파라미터 (엔진 단위: m)
    float FocalDistance = 5.0f;             // m (초점 거리)
    float Fstop = 2.8f;                     // 조리개 값
    float SensorWidth = 0.024f;             // m (센서 너비, 24mm)
    float FocalRegion = 0.5f;               // m (완전 선명 영역)
    float NearTransitionRegion = 2.0f;      // m (근경 블러 전환)
    float FarTransitionRegion = 5.0f;       // m (원경 블러 전환)
    float MaxNearBlurSize = 8.0f;           // pixels (근경 최대 블러)
    float MaxFarBlurSize = 8.0f;            // pixels (원경 최대 블러)

    virtual void ApplyToView(float DeltaTime, FMinimalViewInfo* ViewInfo) override {};

    virtual void CollectPostProcess(TArray<FPostProcessModifier>& Out) override
    {
        if (!bEnabled) return;

        FPostProcessModifier M;
        M.Type = EPostProcessEffectType::DepthOfField;
        M.Priority = Priority;
        M.bEnabled = true;
        M.Weight = Weight;
        M.SourceObject = this;

        // Payload에 DOF 파라미터 담기 (엔진 단위: m)
        // Params0: FocalDistance(m), Fstop, SensorWidth(m), FocalRegion(m)
        M.Payload.Params0 = FVector4(
            FocalDistance,
            Fstop,
            SensorWidth,
            FocalRegion
        );

        // Params1: NearTrans(m), FarTrans(m), MaxNearBlur(px), MaxFarBlur(px)
        M.Payload.Params1 = FVector4(
            NearTransitionRegion,
            FarTransitionRegion,
            MaxNearBlurSize,
            MaxFarBlurSize
        );

        Out.Add(M);
    }
};
