#pragma once
#include "../SceneView.h"

enum class EPostProcessEffectType : uint8
{
    HeightFog,
    Vignette,
    Bloom,
    Fade,
    Gamma,
    DepthOfField,
};

struct FPostProcessPayload
{
    // 쉐이더 CB로 바로 복사될 범용 슬롯
    FVector4 Params0 = FVector4(0,0,0,0);
    FVector4 Params1 = FVector4(0,0,0,0);
    FLinearColor Color = FLinearColor(0, 0, 0, 1);
};

struct FPostProcessModifier
{
    EPostProcessEffectType Type = EPostProcessEffectType::Vignette;
    int32      Priority = 0;
    bool       bEnabled = true;
    float      Weight = 1.0f;
    UObject*   SourceObject = nullptr; // (디버그용) player camera manager나, fog component...
    FPostProcessPayload Payload;       // 범용 슬롯! 효과마다 각 슬롯의 의미가 다름 
};

class IPostProcessPass
{
public:
    virtual ~IPostProcessPass() = default;

    // 해당 모디파이어가 적용 가능한지(Weight 0 등 스킵 케이스)
    virtual bool IsApplicable(const FPostProcessModifier& M) const
    {
        return M.bEnabled && (M.Weight > 0.0f);
    }
    
    // 한 패스를 수행: 여기서 FSwapGuard 생성→Draw→Commit
    virtual void Execute(const FPostProcessModifier& M, FSceneView* View, D3D11RHI* RHIDevice) = 0;
};
