#pragma once
#include "ParticleModule.h"

// TypeData 모듈 기본 클래스
// 파티클의 렌더링 방식을 정의
class UParticleModuleTypeDataBase : public UParticleModule
{
    DECLARE_CLASS(UParticleModuleTypeDataBase, UParticleModule)
public:
    // 렌더링 타입
    EParticleType TypeDataType = EParticleType::Sprite;

    // ============================================================
    // 가상 메서드
    // ============================================================

    // 이 타입에 필요한 파티클 데이터 크기 반환
    virtual int32 GetRequiredParticleBytes() const
    {
        // 기본 타입(Sprite)은 추가 데이터 없음
        return 0;
    }

    // 렌더링을 위한 정점 데이터 크기
    virtual int32 GetDynamicVertexStride() const
    {
        // 기본 Sprite 정점 크기
        return sizeof(FVector) + sizeof(FVector2D) + sizeof(FLinearColor);
    }
};
