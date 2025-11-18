#include "pch.h"
#include "AnimSequence.h"
#include "AnimDateModel.h"

IMPLEMENT_CLASS(UAnimSequence)

UAnimSequence::UAnimSequence() : ArmatureCorrection(FTransform())
{
}

float UAnimSequence::GetPlayLength() const
{
    const UAnimDataModel* Model = GetDataModel();
    if (Model)
    {
        return Model->GetPlayLength();
    }
    return 0.0f;
}

void UAnimSequence::GetAnimationPose(FPoseContext& OutPoseContext, const FAnimExtractContext& ExtractionContext)
{
    GetBonePose(OutPoseContext, ExtractionContext);
}

void UAnimSequence::GetBonePose(FPoseContext& OutPoseContext, const FAnimExtractContext& ExtractionContext)
{
    const UAnimDataModel* Model = GetDataModel();
    if (!Model)
    {
        return;
    }

    const int32 FrameRate = Model->GetFrameRate();
    const int32 NumberOfFrames = Model->GetNumberOfFrames();

    if (NumberOfFrames <= 0 || FrameRate <= 0)
    {
        return;
    }

    // Get current time and convert to frame
    float CurrentTime = static_cast<float>(ExtractionContext.CurrentTime);

    // Handle looping
    if (ExtractionContext.bLooping)
    {
        float PlayLength = Model->GetPlayLength();
        if (PlayLength > 0.0f)
        {
            CurrentTime = FMath::Fmod(CurrentTime, PlayLength);
            if (CurrentTime < 0.0f)
            {
                CurrentTime += PlayLength;
            }
        }
    }

    // 각 본의 전체 키 배열을 그대로 담고 있는 컨테이너를 리턴
    const TArray<FBoneAnimationTrack>& BoneTracks = Model->GetBoneAnimationTracks();

    // Get inverse of Armature correction (to remove Armature transform from animation keys)
    FTransform InverseArmatureCorrection = ArmatureCorrection.Inverse();

    for (int32 TrackIndex = 0; TrackIndex < BoneTracks.Num(); ++TrackIndex)
    {
        const FBoneAnimationTrack& Track = BoneTracks[TrackIndex];
        FName BoneName = Track.Name;

        // 이 본 이름의 트랙 데이터를 주어진 시간(Time)에 맞춰 평가해, 하나의 FTransform으로 돌려주는 함수
        // 1. 본 이름으로 FBoneAnimationTrack을 찾음
        // 2. 애니 전체 프레임 수와 프레임 레이트를 보고, 현재 시간(Time)을 정수 프레임 인덱스 두개(Frame0/Frame1)와 보간 비율로 선형보간 
        // 
        // <선형보간 하는 이유>
        // 애니메이션 키가 프레임 기반으로 저장되어 있기 때문에, 임의의 시간(Time)이 두 키 사이에 걸쳐 있으면 그냥 가까운 키를 그
        // 대로 쓰면 “툭툭 끊겨” 보임. EvaluateBoneTrackTransform은 그걸 부드럽게 만들기 위해 Time을 프레임 단위로 환산하고,
        // 바로 앞/뒤의 두 키(Frame0, Frame1) 값을 가져와서 Alpha 비율만큼 보간 위치·스케일은 선형 보간, 회전은 쿼터니언 Slerp을 씀
        // 애니메이션은 “어느 타이밍에 정확히 이 포즈”같이 명시된 키를 그대로 지켜야 하므로 Apporximation 대신 Interpolation을 사용해야함
        FTransform BoneTransform = Model->EvaluateBoneTrackTransform(BoneName, CurrentTime, true);

        // Apply Armature correction inverse for root bone (first bone in hierarchy)
        // This removes Armature's rotation/scale from Blender FBX, while having no effect on Mixamo (Identity)
        if (TrackIndex == 0)
        {
            // Compose transforms: InverseArmatureCorrection transforms BoneTransform from Armature space to world space
            BoneTransform = InverseArmatureCorrection.GetRelativeTransform(BoneTransform);

            // 믹사모는 Armature를 사용하지 않으므로 바로 TrackIndex가 0일시에 BoneTransform을 그대로 유지한다.

            // 블렌더는 Armature를 사용하므로 TrackIndex가 0일시에 
        }

        // Store in pose context (you'll need to map bone name to index)
        // For now, we'll just use track index
        if (TrackIndex < OutPoseContext.Pose.Num())
        {
            OutPoseContext.Pose[TrackIndex] = BoneTransform;
        }
    }
}