#pragma once
#include "SceneComponent.h"
#include "UAudioComponent.generated.h"

class USound;
struct IXAudio2SourceVoice;

UCLASS(DisplayName="오디오 컴포넌트", Description="사운드를 재생하는 컴포넌트입니다")
class UAudioComponent : public USceneComponent
{
    GENERATED_REFLECTION_BODY()

    UAudioComponent();
    virtual ~UAudioComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime) override;
    virtual void EndPlay() override;

    UFUNCTION(LuaBind, DisplayName="Play", Tooltip="Play the audio")
    void Play();

    UFUNCTION(LuaBind, DisplayName="Stop", Tooltip="Stop the audio")
    void Stop();

    UFUNCTION(LuaBind, DisplayName="PlayOneShot", Tooltip="Play audio from specific slot index")
    void PlaySlot(uint32 SlotIndex);
        
    // Convenience: set first slot
    UFUNCTION(LuaBind, DisplayName="SetSound", Tooltip="Set sound for the first slot")
    void SetSound(USound* NewSound) { if (Sounds.IsEmpty()) Sounds.Add(NewSound); else Sounds[0] = NewSound; }

public:
    // Multiple sounds accessible by index
    UPROPERTY(EditAnywhere, Category="Sound", Tooltip="Array of sound assets to play")
    TArray<USound*> Sounds;

    UPROPERTY(EditAnywhere, Category="Audio", Tooltip="Volume (0..1)")
    float Volume;

    UPROPERTY(EditAnywhere, Category="Audio", Tooltip="Pitch (frequency ratio)")
    float Pitch;

    UPROPERTY(EditAnywhere, Category="Audio", Tooltip="Loop playback")
    bool  bIsLooping;

    UPROPERTY(EditAnywhere, Category="Audio", Tooltip="Auto play on BeginPlay")
    bool  bAutoPlay;

    // Duplication
    virtual void DuplicateSubObjects() override;

protected:
    bool bIsPlaying;
    IXAudio2SourceVoice* SourceVoice = nullptr;
};
