#pragma once
#include "pch.h"

// Per-world render settings (view mode + show flags)
class URenderSettings {
public:
    URenderSettings() = default;
    ~URenderSettings() = default;

    // View mode
    void SetViewMode(EViewMode In) { ViewMode = In; }
    EViewMode GetViewMode() const { return ViewMode; }

    // Show flags
    EEngineShowFlags GetShowFlags() const { return ShowFlags; }
    void SetShowFlags(EEngineShowFlags In) { ShowFlags = In; }
    void EnableShowFlag(EEngineShowFlags Flag) { ShowFlags |= Flag; }
    void DisableShowFlag(EEngineShowFlags Flag) { ShowFlags &= ~Flag; }
    void ToggleShowFlag(EEngineShowFlags Flag) { ShowFlags = HasShowFlag(ShowFlags, Flag) ? (ShowFlags & ~Flag) : (ShowFlags | Flag); }
    bool IsShowFlagEnabled(EEngineShowFlags Flag) const { return HasShowFlag(ShowFlags, Flag); }

    // FXAA parameters
    void SetFXAASpanMax(float Value) { FXAASpanMax = Value; }
    float GetFXAASpanMax() const { return FXAASpanMax; }

    void SetFXAAReduceMul(float Value) { FXAAReduceMul = Value; }
    float GetFXAAReduceMul() const { return FXAAReduceMul; }

    void SetFXAAReduceMin(float Value) { FXAAReduceMin = Value; }
    float GetFXAAReduceMin() const { return FXAAReduceMin; }

    void SetFXAASubPixBlend(float Value) { FXAASubPixBlend = Value; }
    float GetFXAASubPixBlend() const { return FXAASubPixBlend; }

    // Tile-based light culling
    void SetTileSize(uint32 Value) { TileSize = Value; }
    uint32 GetTileSize() const { return TileSize; }

    // 그림자 안티 에일리어싱
    void SetShadowAATechnique(EShadowAATechnique In) { ShadowAATechnique = In; }
    EShadowAATechnique GetShadowAATechnique() const { return ShadowAATechnique; }

private:
    EEngineShowFlags ShowFlags = EEngineShowFlags::SF_DefaultEnabled;
    EViewMode ViewMode = EViewMode::VMI_Lit_Phong;

    // FXAA parameters (NVIDIA FXAA 3.11 style)
    float FXAASpanMax = 8.0f;               // 최대 탐색 범위 (권장: 8.0)
    float FXAAReduceMul = 0.125f;           // 감쇠 승수 (권장: 1/8 = 0.125)
    float FXAAReduceMin = 0.0078125f;       // 최소 감쇠 값 (권장: 1/128 = 0.0078125)
    float FXAASubPixBlend = 0.75f;          // 서브픽셀 블렌딩 강도 (권장: 0.75~1.0)

    // Tile-based light culling
    uint32 TileSize = 16;                   // 타일 크기 (픽셀, 기본값: 16)

    // 그림자 안티 에일리어싱
    EShadowAATechnique ShadowAATechnique = EShadowAATechnique::PCF; // 기본값 PCF
};
