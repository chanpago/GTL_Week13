#pragma once
#include "ParticleModule.h"

// 블렌드 모드
enum class EBlendMode : uint8
{
    Opaque,        // 불투명
    Masked,        // 마스크
    Translucent,   // 반투명
    Additive,      // 가산
    Modulate,      // 곱셈
    Alpha          // 알파 블렌드
};

// 스크린 정렬 방식
enum class EScreenAlignment : uint8
{
    CameraFacing,  // 카메라를 향함 (빌보드)
    Velocity,      // 속도 방향
    LocalSpace     // 로컬 공간
};

// 정렬 모드
enum class ESortMode : uint8
{
    None,          // 정렬 안함
    ByDistance,    // 거리순
    ByAge,         // 생성 시간순
    ViewDepth      // 뷰 깊이순
};

class UParticleModuleRequired : public UParticleModule
{
    DECLARE_CLASS(UParticleModuleRequired, UParticleModule)
public:
    UParticleModuleRequired();

public:
    // ---- Emitter 운영 ----
    int32 MaxParticles = 1000;

    float EmitterDuration = 1.0f;
    float EmitterDelay    = 0.0f;
    int32 EmitterLoops    = 0;     // 0 = infinite (너희 규칙대로)

    // (MVP 편의) 기본 스폰레이트 fallback
    float SpawnRateBase = 10.0f;

    // ---- 공간/종료 규칙 ----
    bool bUseLocalSpace    = false;
    bool bKillOnDeactivate = true;
    bool bKillOnCompleted  = false;

    // ---- 렌더 기본 ----
    UMaterial* Material = nullptr;
    EBlendMode BlendMode = EBlendMode::Alpha;

    EScreenAlignment ScreenAlignment = EScreenAlignment::CameraFacing;
    ESortMode SortMode = ESortMode::ByDistance;

    // ---- (선택) 기본 초기값 fallback ----
    FRawDistributionVector InitialSize;
    FRawDistributionColor  InitialColor;
    FRawDistributionFloat  InitialRotation;
};