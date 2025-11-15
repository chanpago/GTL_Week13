#pragma once
#include "Object.h"

/**
 * @brief 애니메이션 클립이 순수 데이터 모델
 * 모든 종류의 애니메이션 데이터를 표현하는 인터페이스
 * 
 * 예를들어)
 * UAnimSequence가 겉에서 보면 “달리기 애니”, “점프 애니” 같은 에셋인데,
 * 그 “달리기 애니”의 실제 내용물(각 본이 몇 초에 어디로 회전/이동하는지, 커브 값은 뭔지)을 정리해서
 * 들고 있는 내부 데이터 저장소가 UAnimDataModel 역할
 */

struct FRawAnimSequenceTrack
{
    TArray<FVector> PosKeys;   // 위치 키프레임
    TArray<FQuat>   RotKeys;   // 회전 키프레임 (Quaternion)
    TArray<FVector> ScaleKeys; // 스케일 키프레임
};

struct FBoneAnimationTrack
{
    FName Name;                        // Bone 이름
    FRawAnimSequenceTrack InternalTrack; // 실제 애니메이션 데이터
};


class UAnimDataModel : public UObject
{
    DECLARE_CLASS(UAnimDataModel, UObject)

public:
    UAnimDataModel() = default;
    virtual ~UAnimDataModel() = default;


public:
    // Getter functions
    const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const { return BoneAnimationTracks; }
    TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() { return BoneAnimationTracks; }
    float GetPlayLength() const { return PlayLength; }
    int32 GetFrameRate() const { return FrameRate; }
    int32 GetNumberOfFrames() const { return NumberOfFrames; }
    int32 GetNumberOfKeys() const { return NumberOfKeys; }

    // Setter functions (for internal use)
    void SetPlayLength(float InPlayLength) { PlayLength = InPlayLength; }
    void SetFrameRate(int32 InFrameRate) { FrameRate = InFrameRate; }
    void SetNumberOfFrames(int32 InNumberOfFrames) { NumberOfFrames = InNumberOfFrames; }
    void SetNumberOfKeys(int32 InNumberOfKeys) { NumberOfKeys = InNumberOfKeys; }

    // Track management
    int32 GetNumBoneTracks() const { return BoneAnimationTracks.Num(); }
    int32 AddBoneTrack(const FName& BoneName);
    bool RemoveBoneTrack(const FName& BoneName);
    int32 FindBoneTrackIndex(const FName& BoneName) const;
    FBoneAnimationTrack* FindBoneTrack(const FName& BoneName);
    const FBoneAnimationTrack* FindBoneTrack(const FName& BoneName) const;

    // Key data access
    bool SetBoneTrackKeys(const FName& BoneName, const TArray<FVector>& PosKeys, const TArray<FQuat>& RotKeys, const TArray<FVector>& ScaleKeys);
    bool GetBoneTrackTransform(const FName& BoneName, int32 KeyIndex, FTransform& OutTransform) const;

    // Interpolation
    FTransform EvaluateBoneTrackTransform(const FName& BoneName, float Time, bool bInterpolate = true) const;

private:
    TArray<FBoneAnimationTrack> BoneAnimationTracks;
    float PlayLength = 0.0f;
    int32 FrameRate = 30;
    int32 NumberOfFrames = 0;
    int32 NumberOfKeys = 0;


    // 커브 데이터는 주로 애니메이션 블렌딩이나 애니메이션이 다른 시스템과 상호작용할 때 보조 정보로 쓰임
    // 예를 들어 UE의애니 블루프린트처럼 “달릴 때 카메라 흔들림 강도”나 “발 접촉 여부” 같은 값을 애니 커브에 넣어 두고,
    // 재생 중에 그 값을 읽어 와서 블렌딩 가중치, 파티클 효과, 사운드 트리거 등을 제어. 
    //FAnimationCurveData CurveData;

};
