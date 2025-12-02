#pragma once
#include "Widget.h"

class APlayerCameraManager;
class UCamMod_DOF;

/**
 * DOFDebugWidget
 * - DOF 파라미터를 실시간으로 조정할 수 있는 디버그 위젯
 * - PlayerCameraManager의 DOF Modifier를 직접 수정
 */
class UDOFDebugWidget : public UWidget
{
public:
    DECLARE_CLASS(UDOFDebugWidget, UWidget)

    UDOFDebugWidget();
    ~UDOFDebugWidget() override = default;

    void Initialize() override;
    void Update() override;
    void RenderWidget() override;

private:
    // DOF 활성화 여부
    bool bDOFEnabled = false;

    // DOF 파라미터 (로컬 캐시)
    float FocalDistance = 6.0f;           // m
    float FocalRegion = 4.0f;             // m
    float NearTransitionRegion = 5.0f;    // m
    float FarTransitionRegion = 20.0f;    // m
    float MaxNearBlurSize = 32.0f;        // pixels
    float MaxFarBlurSize = 32.0f;         // pixels

    // 헬퍼 함수
    APlayerCameraManager* GetPlayerCameraManager();
    UCamMod_DOF* FindOrCreateDOFModifier();
    void ApplyDOFSettings();
    void SyncFromModifier();
};
