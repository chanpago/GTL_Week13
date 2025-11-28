#pragma once
#include "CameraModifierBase.h"
#include "PostProcessing/PostProcessing.h"

class UCamMod_DOF : public UCameraModifierBase
{
public:
    DECLARE_CLASS(UCamMod_DOF, UCameraModifierBase)

    UCamMod_DOF() = default;
    virtual ~UCamMod_DOF() = default;

    // DOF 파라미터
    float FocalDistance = 500.0f;           // cm (초점 거리)
    float Fstop = 2.8f;                     // 조리개 값
    float SensorWidth = 24.0f;              // mm (센서 너비)
    float FocalRegion = 50.0f;              // cm (완전 선명 영역)
    float NearTransitionRegion = 200.0f;    // cm (근경 블러 전환)
    float FarTransitionRegion = 500.0f;     // cm (원경 블러 전환)
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

        // Payload에 DOF 파라미터 담기
        // Params0: FocalDistance, Fstop, SensorWidth, FocalRegion
        M.Payload.Params0 = FVector4(FocalDistance, Fstop, SensorWidth, FocalRegion);

        // Params1: NearTrans, FarTrans, MaxNearBlur, MaxFarBlur
        M.Payload.Params1 = FVector4(NearTransitionRegion, FarTransitionRegion, MaxNearBlurSize, MaxFarBlurSize);

        Out.Add(M);
    }
};
