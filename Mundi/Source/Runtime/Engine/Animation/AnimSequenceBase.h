#pragma once
#include "AnimationAsset.h"
#include "AnimDateModel.h" 
#include "AnimTypes.h"

class UAnimNotify;
class UAnimNotifyState;
struct FAnimNotifyEvent;
struct FPendingAnimNotify;

/**
 * @brief 제공하는 기능 
 * 애니 데이터는 에셋 단위(UAnimSequenceBase, UAnimDataModel) 에 하나만 존재
 * 2. 재생 시간 : GetPlayLength() - 애니메이션 길이
 * 3. 추출 컨텍스트 : FAnimExtractContext - 시간/루핑 정보 전달
 */


class UAnimSequenceBase : public UAnimationAsset
{
    DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)

public:
    UAnimSequenceBase();
    virtual ~UAnimSequenceBase()  = default;


public:
    UAnimDataModel* GetDataModel() const;

    bool IsNotifyAvailable() const;
    void GetAnimNotify(const float& StartTime, const float& DeltaTime, TArray<FPendingAnimNotify>& OutNotifies) const;
    void GetAnimNotifiesFromDeltaPosition(const float& PreviousPosition, const float& CurrentPosition, TArray<FPendingAnimNotify>& OutNotifies) const;
    TArray<FAnimNotifyEvent>& GetAnimNotifyEvents();
    const TArray<FAnimNotifyEvent>& GetAnimNotifyEvents() const;
    void AddPlaySoundNotify(float Time, UAnimNotify* Notify, float Duration = 0.0f);

    // Save/Load notify metadata as a sidecar JSON (e.g., .anim.json)
    bool SaveMeta(const FString& MetaPathUTF8) const;
    bool LoadMeta(const FString& MetaPathUTF8);

protected:
    
    TArray<FAnimNotifyEvent> Notifies;

    //TArray<FAnimNotifyTrack> AnimNotifyTracks;

    UAnimDataModel* DataModel;

    float RateScale;

    bool bLoop;  
};
