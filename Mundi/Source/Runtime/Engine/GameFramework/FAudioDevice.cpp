// Minimal XAudio2 device bootstrap and 3D helpers
#include "pch.h"
#include "Object.h"
#include "FAudioDevice.h"
#include "../Audio/Sound.h" 

// Static 멤버 변수 정의
IXAudio2* FAudioDevice::pXAudio2 = nullptr;
IXAudio2MasteringVoice* FAudioDevice::pMasteringVoice = nullptr;
X3DAUDIO_HANDLE         FAudioDevice::X3DInstance = {};
X3DAUDIO_LISTENER       FAudioDevice::Listener = {};
DWORD                   FAudioDevice::dwChannelMask = 0;
TArray<IXAudio2SourceVoice*> FAudioDevice::OneShotVoices;

UINT32 FAudioDevice::GetOutputChannelCount()
{
    if (!pMasteringVoice) return 0;
    XAUDIO2_VOICE_DETAILS Details{};
    pMasteringVoice->GetVoiceDetails(&Details);
    return Details.InputChannels; // MasteringVoice의 InputChannels가 실제 출력 채널 수
}
bool FAudioDevice::Initialize()
{
    if (pXAudio2)
    {
        UE_LOG("[Audio] Already initialized");
        return true;
    }

    HRESULT hr = XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !pXAudio2)
    {
        UE_LOG("[Audio] XAudio2Create failed: 0x%08x", static_cast<UINT32>(hr));
        return false;
    }

    hr = pXAudio2->CreateMasteringVoice(&pMasteringVoice);
    if (FAILED(hr) || !pMasteringVoice)
    {
        UE_LOG("[Audio] CreateMasteringVoice failed: 0x%08x", static_cast<UINT32>(hr));
        pXAudio2->Release();
        pXAudio2 = nullptr;
        return false;
    }

    // Get actual channel mask from mastering voice
    DWORD Mask = 0;
    hr = pMasteringVoice->GetChannelMask(&Mask);

    if (SUCCEEDED(hr) && Mask != 0)
    {
        dwChannelMask = Mask;
        UE_LOG("[Audio] Using mastering voice channel mask: 0x%08x", dwChannelMask);
    }
    else
    {
        // Fallback to stereo if we can't get the mask
        dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        UE_LOG("[Audio] Using fallback stereo mask: 0x%08x", dwChannelMask);
    }

    // Initialize X3DAudio BEFORE using it
    hr = X3DAudioInitialize(dwChannelMask, X3DAUDIO_SPEED_OF_SOUND, X3DInstance);
    if (FAILED(hr))
    {
        UE_LOG("[Audio] X3DAudioInitialize failed: 0x%08x", static_cast<UINT32>(hr));
        pMasteringVoice->DestroyVoice();
        pMasteringVoice = nullptr;
        pXAudio2->Release();
        pXAudio2 = nullptr;
        dwChannelMask = 0;
        return false;
    }

    // Initialize listener
    ZeroMemory(&Listener, sizeof(X3DAUDIO_LISTENER));
    Listener.OrientFront.x = 0.0f;
    Listener.OrientFront.y = 1.0f;
    Listener.OrientFront.z = 0.0f;

    Listener.OrientTop.x = 0.0f;
    Listener.OrientTop.y = 0.0f;
    Listener.OrientTop.z = 1.0f;

    Listener.Position.x = 0.0f;
    Listener.Position.y = 0.0f;
    Listener.Position.z = 0.0f;

    Listener.Velocity.x = 0.0f;
    Listener.Velocity.y = 0.0f;
    Listener.Velocity.z = 0.0f;

    // Set cone to nullptr
    Listener.pCone = nullptr;

    UE_LOG("[Audio] XAudio2 initialized successfully. Output channels: %u, ChannelMask: 0x%08x",
        GetOutputChannelCount(), dwChannelMask);

    return true;
}
void FAudioDevice::Shutdown()
{
    // 먼저 재생 중인 모든 SourceVoice 정리
    for (IXAudio2SourceVoice* voice : OneShotVoices)
    {
        if (voice)
        {
            voice->Stop(0);
            voice->FlushSourceBuffers();
            voice->DestroyVoice();
        }
    }
    OneShotVoices.Empty();

    if (pMasteringVoice)
    {
        pMasteringVoice->DestroyVoice();
        pMasteringVoice = nullptr;
    }
    if (pXAudio2)
    {
        pXAudio2->Release();
        pXAudio2 = nullptr;
    }
    ZeroMemory(&Listener, sizeof(Listener));
    ZeroMemory(&X3DInstance, sizeof(X3DInstance));
    dwChannelMask = 0;
}

void FAudioDevice::Update()
{
    if (!pXAudio2) return;

    // For future: process queued commands, streaming, etc.

    // 뒤에서 앞으로 순회하며 완료된 보이스 정리
    for (int i = static_cast<int>(OneShotVoices.size()) - 1; i >= 0; --i)
    {
        IXAudio2SourceVoice* voice = OneShotVoices[i];
        if (!voice) { OneShotVoices.erase(OneShotVoices.begin() + i); continue; }

        XAUDIO2_VOICE_STATE state{};
        voice->GetState(&state);
        if (state.BuffersQueued == 0)
        {
            // Stop -> Flush -> Destroy
            StopSound(voice);
            OneShotVoices.erase(OneShotVoices.begin() + i);
        }
    }
}

IXAudio2SourceVoice* FAudioDevice::PlaySound3D(USound* SoundToPlay, const FVector& EmitterPosition, float Volume, bool bIsLooping)
{
    if (!pXAudio2 || !pMasteringVoice || !SoundToPlay)
        return nullptr;

    const WAVEFORMATEX& fmt = SoundToPlay->GetWaveFormat();
    const uint8* pcm = SoundToPlay->GetPCMData();
    const uint32 size = SoundToPlay->GetPCMSize();
    if (!pcm || size == 0)
        return nullptr;

    IXAudio2SourceVoice* voice = nullptr;
    HRESULT hr = pXAudio2->CreateSourceVoice(&voice, &fmt);
    if (FAILED(hr) || !voice)
    {
        UE_LOG("[Audio] CreateSourceVoice failed: 0x%08x", static_cast<UINT32>(hr));
        return nullptr;
    }

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = pcm;
    buf.AudioBytes = size;
    if (bIsLooping)
    {
        // Loop entire buffer. LoopBegin/LoopLength are in samples (per channel).
        // For PCM, one frame = nBlockAlign bytes (all channels), so per-channel sample count equals frames.
        if (fmt.nBlockAlign > 0)
        {
            const uint32 totalFrames = size / fmt.nBlockAlign;
            if (totalFrames > 0)
            {
                buf.LoopBegin = 0;
                buf.LoopLength = totalFrames; // per-channel samples
                buf.LoopCount = XAUDIO2_LOOP_INFINITE;
                buf.Flags = 0; // do not mark EOS when looping
            }
            else
            {
                buf.Flags = XAUDIO2_END_OF_STREAM;
            }
        }
        else
        {
            buf.Flags = XAUDIO2_END_OF_STREAM;
        }
    }
    else
    {
        buf.Flags = XAUDIO2_END_OF_STREAM;
    }

    hr = voice->SubmitSourceBuffer(&buf);
    if (FAILED(hr))
    {
        UE_LOG("[Audio] SubmitSourceBuffer failed: 0x%08x", static_cast<UINT32>(hr));
        voice->DestroyVoice();
        return nullptr;
    }

    voice->SetVolume(Volume);
    UpdateSoundPosition(voice, EmitterPosition);

    hr = voice->Start(0);
    if (FAILED(hr))
    {
        UE_LOG("[Audio] Start voice failed: 0x%08x", static_cast<UINT32>(hr));
        voice->DestroyVoice();
        return nullptr;
    }

    return voice;
}

void FAudioDevice::SetListenerPosition(const FVector& Position, const FVector& ForwardVec, const FVector& UpVec)
{
    Listener.Position = X3DAUDIO_VECTOR{ Position.X, Position.Y, Position.Z };
    Listener.OrientFront = X3DAUDIO_VECTOR{ ForwardVec.X, ForwardVec.Y, ForwardVec.Z };
    Listener.OrientTop = X3DAUDIO_VECTOR{ UpVec.X, UpVec.Y, UpVec.Z };
}void FAudioDevice::UpdateSoundPosition(IXAudio2SourceVoice* pSourceVoice, const FVector& EmitterPosition)
{
    if (!pSourceVoice || !pXAudio2 || !pMasteringVoice)
    {
        return;
    }

    // Check if X3DAudio is initialized
    if (dwChannelMask == 0)
    {
        UE_LOG("[Audio] X3DAudio not initialized, skipping 3D positioning");
        return;
    }

    XAUDIO2_VOICE_DETAILS Src{};
    pSourceVoice->GetVoiceDetails(&Src);

    XAUDIO2_VOICE_DETAILS Dst{};
    pMasteringVoice->GetVoiceDetails(&Dst);

    if (Src.InputChannels == 0 || Dst.InputChannels == 0)
    {
        return;
    }

    // Allocate matrix
    size_t matrixSize = static_cast<size_t>(Src.InputChannels) * static_cast<size_t>(Dst.InputChannels);
    std::vector<float> Matrix(matrixSize, 0.0f);

    // === SIMPLIFIED EMITTER SETUP ===
    // Use a static array to ensure memory stability
    static float defaultAzimuths[8] = { 0.0f };

    X3DAUDIO_EMITTER Emitter;
    ZeroMemory(&Emitter, sizeof(X3DAUDIO_EMITTER));

    // Position
    Emitter.Position.x = EmitterPosition.X;
    Emitter.Position.y = EmitterPosition.Y;
    Emitter.Position.z = EmitterPosition.Z;

    // Orientation (facing forward, up is Z)
    Emitter.OrientFront.x = 0.0f;
    Emitter.OrientFront.y = 1.0f;
    Emitter.OrientFront.z = 0.0f;

    Emitter.OrientTop.x = 0.0f;
    Emitter.OrientTop.y = 0.0f;
    Emitter.OrientTop.z = 1.0f;

    // Channel setup - THIS IS CRITICAL
    Emitter.ChannelCount = Src.InputChannels;
    Emitter.ChannelRadius = 0.0f; // Use 0.0f for point source

    // For multi-channel sources, provide azimuth array
    if (Src.InputChannels > 1)
    {
        Emitter.pChannelAzimuths = defaultAzimuths;
    }
    else
    {
        Emitter.pChannelAzimuths = nullptr; // Mono source
    }

    // Inner radius setup
    Emitter.InnerRadius = 0.0f;
    Emitter.InnerRadiusAngle = X3DAUDIO_PI / 4.0f; // 45 degrees

    // Curves - all nullptr for default behavior
    Emitter.pVolumeCurve = nullptr;
    Emitter.pLFECurve = nullptr;
    Emitter.pLPFDirectCurve = nullptr;
    Emitter.pLPFReverbCurve = nullptr;
    Emitter.pReverbCurve = nullptr;

    // Scaling
    Emitter.CurveDistanceScaler = 14.0f; // Adjust for your world scale
    Emitter.DopplerScaler = 1.0f;

    // No cone (omnidirectional)
    Emitter.pCone = nullptr;

    // === DSP SETTINGS ===
    X3DAUDIO_DSP_SETTINGS Dsp;
    ZeroMemory(&Dsp, sizeof(X3DAUDIO_DSP_SETTINGS));

    Dsp.SrcChannelCount = Src.InputChannels;
    Dsp.DstChannelCount = Dst.InputChannels;
    Dsp.pMatrixCoefficients = Matrix.data();
    Dsp.pDelayTimes = nullptr; // Not using delay

    // === VERIFY LISTENER ===
    // Make sure Listener is properly initialized
    if (Listener.pCone != nullptr)
    {
        Listener.pCone = nullptr; // Force nullptr
    }

    // Normalize listener orientation vectors
    float frontLen = sqrtf(Listener.OrientFront.x * Listener.OrientFront.x +
        Listener.OrientFront.y * Listener.OrientFront.y +
        Listener.OrientFront.z * Listener.OrientFront.z);
    if (frontLen < 0.001f)
    {
        // Invalid listener orientation, reset to default
        Listener.OrientFront.x = 0.0f;
        Listener.OrientFront.y = 1.0f;
        Listener.OrientFront.z = 0.0f;

        Listener.OrientTop.x = 0.0f;
        Listener.OrientTop.y = 0.0f;
        Listener.OrientTop.z = 1.0f;
    }

    // === CALL X3DAudioCalculate ===
    try
    {
        X3DAudioCalculate(
            X3DInstance,
            &Listener,
            &Emitter,
            X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER,
            &Dsp
        );
    }
    catch (...)
    {
        UE_LOG("[Audio] X3DAudioCalculate exception caught!");
        return;
    }

    // === APPLY RESULTS ===
    HRESULT hr = pSourceVoice->SetOutputMatrix(
        pMasteringVoice,
        Src.InputChannels,
        Dst.InputChannels,
        Dsp.pMatrixCoefficients
    );

    if (FAILED(hr))
    {
        UE_LOG("[Audio] SetOutputMatrix failed: 0x%08x", static_cast<UINT32>(hr));
        return;
    }

    // Apply Doppler if valid
    if (Dsp.DopplerFactor > 0.0f && Dsp.DopplerFactor < 4.0f)
    {
        pSourceVoice->SetFrequencyRatio(Dsp.DopplerFactor);
    }
}
void FAudioDevice::PlaySoundAtLocationOneShot(USound* Sound, const FVector& Pos, float Volume, float Pitch)
{
    if (!pXAudio2 || !pMasteringVoice || !Sound) return;

    const WAVEFORMATEX& fmt = Sound->GetWaveFormat();
    const uint8* pcm = Sound->GetPCMData();
    const uint32 size = Sound->GetPCMSize();
    if (!pcm || size == 0) return;

    IXAudio2SourceVoice* voice = nullptr;
    HRESULT hr = pXAudio2->CreateSourceVoice(&voice, &fmt);
    if (FAILED(hr) || !voice) return;

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = pcm;
    buf.AudioBytes = size;
    buf.Flags = XAUDIO2_END_OF_STREAM;

    hr = voice->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) { voice->DestroyVoice(); return; }

    voice->SetVolume(Volume);
    if (Pitch > 0.f) voice->SetFrequencyRatio(Pitch);

    // 고정 위치 3D 매트릭스 1회 적용(팔로우 없음)
    UpdateSoundPosition(voice, Pos);

    hr = voice->Start(0);
    if (FAILED(hr)) { voice->DestroyVoice(); return; }

    OneShotVoices.push_back(voice);
}
void FAudioDevice::StopSound(IXAudio2SourceVoice* pSourceVoice)
{
    if (!pSourceVoice || !pXAudio2) return;
    pSourceVoice->Stop(0);
    pSourceVoice->FlushSourceBuffers();
    pSourceVoice->DestroyVoice();
}

void FAudioDevice::Preload()
{
    const fs::path DataDir(GDataDir + "/Audio");
    if (!fs::exists(DataDir) || !fs::is_directory(DataDir))
    {
        UE_LOG("FAudioDevice::Preload: Data directory not found: %s", DataDir.string().c_str());
        return;
    }

    size_t LoadedCount = 0;
    std::unordered_set<FString> ProcessedFiles; //중복 로딩 방지

    for (const auto& Entry : fs::recursive_directory_iterator(DataDir))
    {
        if (!Entry.is_regular_file())
            continue;

        const fs::path& Path = Entry.path();
        FString Extension = Path.extension().string();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (Extension == ".wav")
        {
            FString PathStr = NormalizePath(Path.string());

            // 이미 처리된 파일인지 확인
            if (ProcessedFiles.find(PathStr) == ProcessedFiles.end())
            {
                ProcessedFiles.insert(PathStr);
                // Load wav file 
                ++LoadedCount;
                UResourceManager::GetInstance().Load<USound>(Path.string());
            }
        }
    }
    RESOURCE.SetAudioFiles();

    UE_LOG("FAudioDevice::Preload: Loaded %zu .wav files from %s", LoadedCount, DataDir.string().c_str());
}
