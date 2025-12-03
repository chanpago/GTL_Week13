// Audio component that plays USound via FAudioDevice

#include "pch.h"
#include "AudioComponent.h"
#include "../GameFramework/FAudioDevice.h"
#include "../Audio/Sound.h"
#include "../../Core/Misc/PathUtils.h"
#include <xaudio2.h>
#include "ResourceManager.h"
#include "LuaBindHelpers.h"

//extern "C" void LuaBind_Anchor_UAudioComponent() {}

// IMPLEMENT_CLASS is now auto-generated in UAudioComponent.generated.cpp

UAudioComponent::UAudioComponent()
    : Volume(1.0f)
    , Pitch(1.0f)
    , bIsLooping(false)
    , bAutoPlay(true)
    , bIsPlaying(false)
{
}

UAudioComponent::~UAudioComponent()
{
    Stop();
}

void UAudioComponent::BeginPlay()
{
    Super::BeginPlay();
    if (bAutoPlay)
    {
        Play();
    }
}

void UAudioComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (bIsPlaying && SourceVoice && FAudioDevice::IsInitialized())
    {
        FVector CurrentLocation = GetWorldLocation();
        FAudioDevice::UpdateSoundPosition(SourceVoice, CurrentLocation);

        if (!bIsLooping)
        {
            XAUDIO2_VOICE_STATE state{};
            SourceVoice->GetState(&state);
            if (state.BuffersQueued == 0)
            {
                Stop();
            }
        }
    }
}

void UAudioComponent::EndPlay()
{
    Super::EndPlay();
    Stop();
} 

void UAudioComponent::Play()
{
    // default to first valid slot
    for (uint32 i = 0; i < Sounds.Num(); ++i)
    {
        if (Sounds[i]) { PlaySlot(i); return; }
    }
}

void UAudioComponent::Stop()
{
    if (!bIsPlaying)
        return;

    if (SourceVoice)
    {
        FAudioDevice::StopSound(SourceVoice);
        SourceVoice = nullptr;
    }
    bIsPlaying = false;
}

void UAudioComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}


void UAudioComponent::PlaySlot(uint32 SlotIndex)
{
    if (SlotIndex >= (uint32)Sounds.Num()) return;
    USound* Selected = Sounds[SlotIndex];
    if (!Selected) return;
    //if (bIsPlaying) return;

    FVector CurrentLocation = GetWorldLocation();
    SourceVoice = FAudioDevice::PlaySound3D(Selected, CurrentLocation, Volume, bIsLooping);
    if (SourceVoice)
    {
        SourceVoice->SetFrequencyRatio(Pitch);
        bIsPlaying = true;
    }
}
