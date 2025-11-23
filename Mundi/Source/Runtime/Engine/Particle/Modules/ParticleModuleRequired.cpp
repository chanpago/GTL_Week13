#include "pch.h"
#include "ParticleModuleRequired.h"

IMPLEMENT_CLASS(UParticleModuleRequired)

UParticleModuleRequired::UParticleModuleRequired()
{
    // 기본 색상을 흰색으로 설정 (텍스처가 제대로 보이도록)
    InitialColor = FRawDistributionColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
    // 기본 크기 설정
    InitialSize = FRawDistributionVector(FVector(50.0f, 50.0f, 1.0f));
}
