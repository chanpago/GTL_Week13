#include "pch.h"
#include "SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Animation/AnimDateModel.h"
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"
#include "Source/Runtime/Engine/Animation/AnimSingleNodeInstance.h"
#include "Source/Runtime/Engine/Animation/AnimTypes.h"
#include "Source/Runtime/Engine/Animation/AnimationAsset.h"
#include "Source/Runtime/Engine/Animation/AnimNotify_PlaySound.h"
#include "Source/Runtime/Engine/Animation/Team2AnimInstance.h"
#include "InputManager.h"

#include "PlatformTime.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{ 
    SetSkeletalMesh("Data/James/James.fbx");
}


void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    // Team2AnimInstance를 사용한 상태머신 기반 애니메이션 시스템
    // AnimationBlueprint 모드로 전환
    SetAnimationMode(EAnimationMode::AnimationBlueprint);

    // Team2AnimInstance 생성 및 설정
    UTeam2AnimInstance* Team2AnimInst = NewObject<UTeam2AnimInstance>();
    SetAnimInstance(Team2AnimInst);

    UE_LOG("Team2AnimInstance initialized - Idle/Walk/Run state machine ready");
    UE_LOG("Use SetMovementSpeed() to control animation transitions");
    UE_LOG("  Speed < 0.1: Idle animation");
    UE_LOG("  Speed 0.1 ~ 5.0: Walk animation");
    UE_LOG("  Speed >= 5.0: Run animation");
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!SkeletalMesh) { return; }

    // Team2AnimInstance 테스트를 위한 키 입력 처리
    if (AnimInstance)
    {
        //UE_LOG("Tick Component Running");

        
        UInputManager& InputManager = UInputManager::GetInstance();

        static float CurrentSpeed = 0.0f;
        static float LastLoggedSpeed = -1.0f;

        // W 키: 속도 증가 (Walk -> Run 전환 테스트)
        if (InputManager.IsKeyDown('W'))
        {
            CurrentSpeed += DeltaTime * 5.0f; // 초당 5.0 증가
            CurrentSpeed = FMath::Min(CurrentSpeed, 10.0f); // 최대 10.0
            AnimInstance->SetMovementSpeed(CurrentSpeed);

            // 0.5 단위로 속도가 변경될 때마다 로그 출력
            if (FMath::Abs(CurrentSpeed - LastLoggedSpeed) >= 0.5f)
            {
                UE_LOG("[Team2AnimInstance] Speed: %.2f (W key - Increasing)", CurrentSpeed);
                LastLoggedSpeed = CurrentSpeed;
            }
        }
        // S 키: 속도 감소 (Run -> Walk 전환 테스트)
        else if (InputManager.IsKeyDown('S'))
        {
            CurrentSpeed -= DeltaTime * 5.0f; // 초당 5.0 감소
            CurrentSpeed = FMath::Max(CurrentSpeed, 0.0f); // 최소 0.0
            AnimInstance->SetMovementSpeed(CurrentSpeed);

            // 0.5 단위로 속도가 변경될 때마다 로그 출력
            if (FMath::Abs(CurrentSpeed - LastLoggedSpeed) >= 0.5f)
            {
                UE_LOG("[Team2AnimInstance] Speed: %.2f (S key - Decreasing)", CurrentSpeed);
                LastLoggedSpeed = CurrentSpeed;
            }
        }
        // R 키: 속도 리셋 (한 번만 눌렀을 때)
        else if (InputManager.IsKeyPressed('R'))
        {
            CurrentSpeed = 0.0f;
            AnimInstance->SetMovementSpeed(CurrentSpeed);
            UE_LOG("[Team2AnimInstance] Speed RESET to 0.0");
            LastLoggedSpeed = CurrentSpeed;
        }

        // 현재 속도 상태를 주기적으로 로그 (5초마다)
        static float LogTimer = 0.0f;
        LogTimer += DeltaTime;
        if (LogTimer >= 5.0f)
        {
            UE_LOG("[Team2AnimInstance] Current Speed: %.2f, IsMoving: %d, Threshold: 5.0",
                CurrentSpeed,
                AnimInstance->GetIsMoving() ? 1 : 0);
            LogTimer = 0.0f;
        }

        // AnimInstance가 NativeUpdateAnimation에서:
        // 1. 상태머신 업데이트 (있다면)
        // 2. 시간 갱신 및 루핑 처리
        // 3. 포즈 평가 및 SetAnimationPose() 호출
        // 4. 노티파이 트리거
        // 5. 커브 업데이트
        AnimInstance->NativeUpdateAnimation(DeltaTime);
    }
    else
    {
        // 레거시 경로: AnimInstance 없이 직접 애니메이션 업데이트
        // (호환성 유지를 위해 남겨둠, 추후 제거 예정)
        TickAnimation(DeltaTime);
    }
}

void USkeletalMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
    Super::SetSkeletalMesh(PathFileName);

    if (SkeletalMesh && SkeletalMesh->GetSkeletalMeshData())
    {
        const FSkeleton& Skeleton = SkeletalMesh->GetSkeletalMeshData()->Skeleton;
        const int32 NumBones = Skeleton.Bones.Num();

        CurrentLocalSpacePose.SetNum(NumBones);
        CurrentComponentSpacePose.SetNum(NumBones);
        TempFinalSkinningMatrices.SetNum(NumBones);
        TempFinalSkinningNormalMatrices.SetNum(NumBones);

        for (int32 i = 0; i < NumBones; ++i)
        {
            const FBone& ThisBone = Skeleton.Bones[i];
            const int32 ParentIndex = ThisBone.ParentIndex;
            FMatrix LocalBindMatrix;

            if (ParentIndex == -1) // 루트 본
            {
                LocalBindMatrix = ThisBone.BindPose;
            }
            else // 자식 본
            {
                const FMatrix& ParentInverseBindPose = Skeleton.Bones[ParentIndex].InverseBindPose;
                LocalBindMatrix = ThisBone.BindPose * ParentInverseBindPose;
            }
            // 계산된 로컬 행렬을 로컬 트랜스폼으로 변환
            CurrentLocalSpacePose[i] = FTransform(LocalBindMatrix); 
        }
        
        ForceRecomputePose(); 
    }
    else
    {
        // 메시 로드 실패 시 버퍼 비우기
        CurrentLocalSpacePose.Empty();
        CurrentComponentSpacePose.Empty();
        TempFinalSkinningMatrices.Empty();
        TempFinalSkinningNormalMatrices.Empty();
    }
}

FAABB USkeletalMeshComponent::GetWorldAABB() const
{
    return Super::GetWorldAABB();
}

void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InAnimationMode)
{
    if (AnimationMode == InAnimationMode)
    {
        return; // 이미 해당 모드
    }

    AnimationMode = InAnimationMode;

    if (AnimationMode == EAnimationMode::AnimationSingleNode)
    {
        // SingleNode 모드: UAnimSingleNodeInstance 생성
        UAnimSingleNodeInstance* SingleNodeInstance = NewObject<UAnimSingleNodeInstance>();
        SetAnimInstance(SingleNodeInstance);

        UE_LOG("SetAnimationMode: Switched to AnimationSingleNode mode");
    }
    else if (AnimationMode == EAnimationMode::AnimationBlueprint)
    {
        // AnimationBlueprint 모드: 커스텀 AnimInstance 설정 대기
        // (사용자가 SetAnimInstance로 상태머신이 포함된 인스턴스를 설정해야 함)
        UE_LOG("SetAnimationMode: Switched to AnimationBlueprint mode");
    }
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
    if (!NewAnimToPlay)
    {
        StopAnimation();
        return;
    }

    // SingleNode 모드로 전환 (AnimSingleNodeInstance 자동 생성)
    SetAnimationMode(EAnimationMode::AnimationSingleNode);

    UAnimSequence* Sequence = Cast<UAnimSequence>(NewAnimToPlay);
    if (!Sequence)
    {
        UE_LOG("PlayAnimation: Only UAnimSequence assets are supported currently");
        return;
    }

    // AnimSingleNodeInstance를 통해 재생
    UAnimSingleNodeInstance* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);
    if (SingleNodeInstance)
    {
        SingleNodeInstance->PlaySingleNode(Sequence, bLooping, 1.0f);
        UE_LOG("PlayAnimation: Playing through AnimSingleNodeInstance");
    }
    else
    {
        // Fallback: 직접 재생
        SetAnimation(Sequence);
        SetLooping(bLooping);
        SetPlayRate(1.0f);
        CurrentAnimationTime = 0.0f;
        SetPlaying(true);
        UE_LOG("PlayAnimation: Playing directly (fallback)");
    }
}

void USkeletalMeshComponent::StopAnimation()
{
    SetPlaying(false);
    CurrentAnimationTime = 0.0f;
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& NewLocalTransform)
{
    if (CurrentLocalSpacePose.Num() > BoneIndex)
    {
        CurrentLocalSpacePose[BoneIndex] = NewLocalTransform;
        ForceRecomputePose();
    }
}

void USkeletalMeshComponent::SetBoneWorldTransform(int32 BoneIndex, const FTransform& NewWorldTransform)
{
    if (BoneIndex < 0 || BoneIndex >= CurrentLocalSpacePose.Num())
        return;

    const int32 ParentIndex = SkeletalMesh->GetSkeleton()->Bones[BoneIndex].ParentIndex;

    const FTransform& ParentWorldTransform = GetBoneWorldTransform(ParentIndex);
    FTransform DesiredLocal = ParentWorldTransform.GetRelativeTransform(NewWorldTransform);

    SetBoneLocalTransform(BoneIndex, DesiredLocal);
}


FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
    if (CurrentLocalSpacePose.Num() > BoneIndex)
    {
        return CurrentLocalSpacePose[BoneIndex];
    }
    return FTransform();
}

FTransform USkeletalMeshComponent::GetBoneWorldTransform(int32 BoneIndex)
{
    if (CurrentLocalSpacePose.Num() > BoneIndex && BoneIndex >= 0)
    {
        // 뼈의 컴포넌트 공간 트랜스폼 * 컴포넌트의 월드 트랜스폼
        return GetWorldTransform().GetWorldTransform(CurrentComponentSpacePose[BoneIndex]);
    }
    return GetWorldTransform(); // 실패 시 컴포넌트 위치 반환
}

void USkeletalMeshComponent::ForceRecomputePose()
{
    if (!SkeletalMesh) { return; } 

    // LocalSpace -> ComponentSpace 계산
    UpdateComponentSpaceTransforms();
    // ComponentSpace -> Final Skinning Matrices 계산
    UpdateFinalSkinningMatrices();
    UpdateSkinningMatrices(TempFinalSkinningMatrices, TempFinalSkinningNormalMatrices);    
    {
        TIME_PROFILE(SkeletalAABB)
        // GetWorldAABB 함수에서 AABB를 갱신중
        GetWorldAABB();
        TIME_PROFILE_END(SkeletalAABB)
    }
    
    PerformSkinning();
    
}

void USkeletalMeshComponent::UpdateComponentSpaceTransforms()
{
    const FSkeleton& Skeleton = SkeletalMesh->GetSkeletalMeshData()->Skeleton;
    const int32 NumBones = Skeleton.Bones.Num();

    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        const FTransform& LocalTransform = CurrentLocalSpacePose[BoneIndex];
        const int32 ParentIndex = Skeleton.Bones[BoneIndex].ParentIndex;

        if (ParentIndex == -1) // 루트 본
        {
            CurrentComponentSpacePose[BoneIndex] = LocalTransform;
        }
        else // 자식 본
        {
            const FTransform& ParentComponentTransform = CurrentComponentSpacePose[ParentIndex];
            CurrentComponentSpacePose[BoneIndex] = ParentComponentTransform.GetWorldTransform(LocalTransform);
        }
    }
}

void USkeletalMeshComponent::UpdateFinalSkinningMatrices()
{
    const FSkeleton& Skeleton = SkeletalMesh->GetSkeletalMeshData()->Skeleton;
    const int32 NumBones = Skeleton.Bones.Num();

    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        const FMatrix& InvBindPose = Skeleton.Bones[BoneIndex].InverseBindPose;
        const FMatrix ComponentPoseMatrix = CurrentComponentSpacePose[BoneIndex].ToMatrix();

        TempFinalSkinningMatrices[BoneIndex] = InvBindPose * ComponentPoseMatrix;
        TempFinalSkinningNormalMatrices[BoneIndex] = TempFinalSkinningMatrices[BoneIndex].Inverse().Transpose();
    }
}

// ============================================================
// Animation Section
// ============================================================

void USkeletalMeshComponent::SetAnimation(UAnimSequence* InAnimation)
{
    if (CurrentAnimation != InAnimation)
    {
        CurrentAnimation = InAnimation;
        CurrentAnimationTime = 0.0f;

        if (CurrentAnimation)
        {
            // 애니메이션이 설정되면 자동으로 재생 시작
            bIsPlaying = true;
        }
    }
}

void USkeletalMeshComponent::SetAnimationTime(float InTime)
{
    if (CurrentAnimationTime == InTime)
    {
        return;
    }
    
    CurrentAnimationTime = InTime;

    if (CurrentAnimation)
    {
        float PlayLength = CurrentAnimation->GetPlayLength();

        // 루핑 처리
        if (bIsLooping)
        {
            while (CurrentAnimationTime < 0.0f)
            {
                CurrentAnimationTime += PlayLength;
            }
            while (CurrentAnimationTime > PlayLength)
            {
                CurrentAnimationTime -= PlayLength;
            }
        }
        else
        {
            // 범위 제한
            CurrentAnimationTime = FMath::Clamp(CurrentAnimationTime, 0.0f, PlayLength);
        }



        GatherNotifiesFromRange(PrevAnimationTime, CurrentAnimationTime);
        
        /* @todo 애니메이션 갱신을 위해 추가함. 이후, 애니메이션 계산 로직만을 별도로 분리해야 할 필요 존재함 */
        TickAnimInstances(0.0f);

        DispatchAnimNotifies();

        PrevAnimationTime = CurrentAnimationTime;
    }
}

void USkeletalMeshComponent::GatherNotifies(float DeltaTime)
{ 
    if (!CurrentAnimation)
    {
        return;
    }
  
    // 이전 틱에 저장 된 PendingNotifies 지우고 시작
    PendingNotifies.Empty();

    // 시간 업데이트
    const float PrevTime = CurrentAnimationTime;
    const float DeltaMove = DeltaTime * PlayRate;

    // 이번 틱 구간 [PrevTime -> PrevTime + DeltaMove]에서 발생한 Notify 수집 
    CurrentAnimation->GetAnimNotify(PrevTime, DeltaMove, PendingNotifies);
}

void USkeletalMeshComponent::GatherNotifiesFromRange(float PrevTime, float CurTime)
{
    if (!CurrentAnimation)
    {
        return;
    }

    // 이전 틱에 저장 된 PendingNotifies 지우고 시작
    PendingNotifies.Empty();

    // 시간 업데이트
    float DeltaTime = CurTime - PrevTime;
    const float DeltaMove = DeltaTime * PlayRate;

    // 이번 틱 구간 [PrevTime -> PrevTime + DeltaMove]에서 발생한 Notify 수집 
    CurrentAnimation->GetAnimNotify(PrevTime, DeltaMove, PendingNotifies);
}

void USkeletalMeshComponent::DispatchAnimNotifies()
{
    for (const FPendingAnimNotify& Pending : PendingNotifies)
    {
        const FAnimNotifyEvent& Event = *Pending.Event;

        switch (Pending.Type)
        {
        case EPendingNotifyType::Trigger:
            if (Event.Notify)
            {
                Event.Notify->Notify(this, CurrentAnimation); 
            }
            break;
            
        case EPendingNotifyType::StateBegin:
            if (Event.NotifyState)
            {
                Event.NotifyState->NotifyBegin(this, CurrentAnimation, Event.Duration);
            }
            break;

        case EPendingNotifyType::StateTick:
            if(Event.NotifyState)
            {
                Event.NotifyState->NotifyTick(this, CurrentAnimation, Event.Duration);
            }
            break;
        case EPendingNotifyType::StateEnd:
            if (Event.NotifyState)
            {
                Event.NotifyState->NotifyEnd(this, CurrentAnimation, Event.Duration);
            }
            break;

        default:
            break;
        }
         
    }
}

void USkeletalMeshComponent::TickAnimation(float DeltaTime)
{
    if (!ShouldTickAnimation())
    {
        static bool bLoggedOnce = false;
        if (!bLoggedOnce)
        {
            UE_LOG("TickAnimation skipped - CurrentAnimation: %p, bIsPlaying: %d", CurrentAnimation, bIsPlaying);
            bLoggedOnce = true;
        }
        return;
    }

    GatherNotifies(DeltaTime);

    TickAnimInstances(DeltaTime); 
    
    DispatchAnimNotifies();
}

bool USkeletalMeshComponent::ShouldTickAnimation() const
{
    return CurrentAnimation != nullptr && bIsPlaying;
}

void USkeletalMeshComponent::TickAnimInstances(float DeltaTime)
{
    if (!CurrentAnimation || !bIsPlaying)
    {
        return;
    }

    CurrentAnimationTime += DeltaTime * PlayRate;

    //UE_LOG("CurrentAnimationTime %.2f", CurrentAnimationTime);

    float PlayLength = CurrentAnimation->GetPlayLength();

    static int FrameCount = 0;
    if (FrameCount++ % 60 == 0) // 매 60프레임마다 로그
    {
        UE_LOG("Animation Playing - Time: %.2f / %.2f, Looping: %d", CurrentAnimationTime, PlayLength, bIsLooping);
    }

    // 2. 루핑 처리
    if (bIsLooping)
    {
        if (CurrentAnimationTime >= PlayLength)
        {
            CurrentAnimationTime = FMath::Fmod(CurrentAnimationTime, PlayLength);
        }
    }
    else
    {
        // 애니메이션 끝에 도달하면 정지
        if (CurrentAnimationTime >= PlayLength)
        {
            CurrentAnimationTime = PlayLength;
            bIsPlaying = false;
        }
    } 

    // 3. 현재 시간의 포즈 추출
    UAnimDataModel* DataModel = CurrentAnimation->GetDataModel();
    if (!DataModel)
    {
        return;
    }

    const FSkeleton& Skeleton = SkeletalMesh->GetSkeletalMeshData()->Skeleton;
    //CurrentAnimation->SetSkeleton(Skeleton);


    int32 NumBones = DataModel->GetNumBoneTracks();

    // 4. 각 본의 애니메이션 포즈 적용
    FAnimExtractContext ExtractContext(CurrentAnimationTime, bIsLooping);
    FPoseContext PoseContext(NumBones);

    // 현재 재생시간과 루핑 정보를 담은 ExtractContext 구조체를 기반으로 GetAnimationPose에서 현재 시간에 맞는 본의 행렬을 반환한다
    CurrentAnimation->GetAnimationPose(PoseContext, ExtractContext);

    // 5. 추출된 포즈를 CurrentLocalSpacePose에 적용
    const TArray<FBoneAnimationTrack>& BoneTracks = DataModel->GetBoneAnimationTracks();

    static bool bLoggedBoneMatching = false;
    static bool bLoggedAnimData = false;
    int32 MatchedBones = 0;
    int32 TotalBones = BoneTracks.Num();

    for (int32 TrackIdx = 0; TrackIdx < BoneTracks.Num(); ++TrackIdx)
    {
        const FBoneAnimationTrack& Track = BoneTracks[TrackIdx];

        // 스켈레톤에서 본 인덱스 찾기
        int32 BoneIndex = Skeleton.FindBoneIndex(Track.Name);

        if (BoneIndex != INDEX_NONE && BoneIndex < CurrentLocalSpacePose.Num())
        {
            // 애니메이션 포즈 적용
            CurrentLocalSpacePose[BoneIndex] = PoseContext.Pose[TrackIdx];
            MatchedBones++;

            // 첫 5개 본의 애니메이션 데이터 로그
            if (!bLoggedAnimData && BoneIndex < 5)
            {
                const FTransform& AnimTransform = PoseContext.Pose[TrackIdx];
                UE_LOG("[AnimData] Bone[%d] %s: T(%.3f,%.3f,%.3f) R(%.3f,%.3f,%.3f,%.3f) S(%.3f,%.3f,%.3f)",
                    BoneIndex, Track.Name.ToString().c_str(),
                    AnimTransform.Translation.X, AnimTransform.Translation.Y, AnimTransform.Translation.Z,
                    AnimTransform.Rotation.X, AnimTransform.Rotation.Y, AnimTransform.Rotation.Z, AnimTransform.Rotation.W,
                    AnimTransform.Scale3D.X, AnimTransform.Scale3D.Y, AnimTransform.Scale3D.Z);
            }
        }
        else if (!bLoggedBoneMatching)
        {
            UE_LOG("Bone not found in skeleton: %s (TrackIdx: %d)", Track.Name.ToString().c_str(), TrackIdx);
        }
    }

    if (!bLoggedAnimData && MatchedBones > 0)
    {
        bLoggedAnimData = true;
    }

    if (!bLoggedBoneMatching)
    {
        UE_LOG("Bone matching: %d / %d bones matched", MatchedBones, TotalBones);
        UE_LOG("Skeleton has %d bones, Animation has %d tracks", Skeleton.Bones.Num(), TotalBones);

        // Print first 5 bone names from each
        UE_LOG("=== Skeleton Bones (first 5) ===");
        for (int32 i = 0; i < FMath::Min(5, (int32)Skeleton.Bones.Num()); ++i)
        {
            UE_LOG("  [%d] %s", i, Skeleton.Bones[i].Name.c_str());
        }

        UE_LOG("=== Animation Tracks (first 5) ===");
        for (int32 i = 0; i < FMath::Min(5, (int32)BoneTracks.Num()); ++i)
        {
            UE_LOG("  [%d] %s", i, BoneTracks[i].Name.ToString().c_str());
        }

        bLoggedBoneMatching = true;
    }

    // 6. 포즈 변경 사항을 스키닝에 반영
    ForceRecomputePose();
}

// ============================================================
// AnimInstance Integration
// ============================================================

void USkeletalMeshComponent::SetAnimationPose(const TArray<FTransform>& InPose)
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshData())
    {
        return;
    }

    const FSkeleton& Skeleton = SkeletalMesh->GetSkeletalMeshData()->Skeleton;
    const int32 NumBones = Skeleton.Bones.Num();

    // 포즈가 스켈레톤과 일치하는지 확인
    if (InPose.Num() != NumBones)
    {
        UE_LOG("SetAnimationPose: Pose size mismatch (%d != %d)", InPose.Num(), NumBones);
        return;
    }

    // AnimInstance가 계산한 포즈를 CurrentLocalSpacePose에 복사
    // 주의: AnimInstance의 포즈는 본 트랙 순서이므로 스켈레톤 본 순서와 매칭해야 함
    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        if (BoneIndex < InPose.Num())
        {
            CurrentLocalSpacePose[BoneIndex] = InPose[BoneIndex];
        }
    }

    // 포즈 변경 사항을 스키닝에 반영
    ForceRecomputePose();
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InAnimInstance)
{
    AnimInstance = InAnimInstance;

    if (AnimInstance)
    {
        AnimInstance->Initialize(this);
        UE_LOG("AnimInstance initialized for SkeletalMeshComponent");
    }
}

