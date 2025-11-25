#pragma once

struct FParticleSimulationContext
{
    // 시간 정보
    float DeltaTime;
    float RealTimeSeconds;

    // 공간 정보
    FVector ComponentLocation;
    FQuat   ComponentRotation;
    FVector ComponentScale;
    FMatrix WorldToLocal; // 스폰 위치 계산용
    FMatrix LocalToWorld; // 월드 변환용

    // 상태 정보
    bool bIsActive;
    bool bSuppressSpawning;
    int32 CurrentLODIndex;

    // 기타
    FVector CameraLocation;
};
