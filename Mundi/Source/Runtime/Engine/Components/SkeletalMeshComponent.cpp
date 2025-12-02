#include "pch.h"
#include "SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Animation/AnimDateModel.h"
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"
#include "Source/Runtime/Engine/Animation/AnimationStateMachine.h"
#include "Source/Runtime/Engine/Animation/AnimSingleNodeInstance.h"
#include "Source/Runtime/Engine/Animation/AnimTypes.h"
#include "Source/Runtime/Engine/Animation/AnimationAsset.h"
#include "Source/Runtime/Engine/Animation/AnimNotify_PlaySound.h"
#include "Source/Runtime/Engine/Animation/Team2AnimInstance.h"
#include "Source/Runtime/Core/Misc/PathUtils.h"
#include "Source/Runtime/Core/Misc/JsonSerializer.h"
#include "Source/Editor/BlueprintGraph/AnimationGraph.h"
#include "InputManager.h"

#include "Source/Runtime/AssetManagement/ResourceManager.h"

#include "PlatformTime.h"
#include "USlateManager.h"
#include "BlueprintGraph/AnimBlueprintCompiler.h"

#include "Source/Runtime/Engine/Physics/PhysScene.h"
#include "Source/Runtime/Engine/Physics/BodyInstance.h"
#include "Source/Runtime/Engine/Physics/ConstraintInstance.h"
#include "Source/Runtime/Engine/Physics/BodySetup.h"
#include "Source/Runtime/Engine/Physics/PhysicsTypes.h"
#include "Source/Runtime/Engine/Physics/PhysicsAsset.h"
#include "World.h"


static FBodyInstance* FindBodyInstanceByName(const TArray<FBodyInstance*>& Bodies, const FName& BoneName)
{
    for (FBodyInstance* BI : Bodies)
    {
        if (BI && BI->BodySetup && BI->BodySetup->BoneName == BoneName)
        {
            return BI;
        }
    }
    return nullptr;
}

USkeletalMeshComponent::USkeletalMeshComponent()
{ 
    SetSkeletalMesh("Data/James/James.fbx");
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    if (PhysicsAssetOverride)
    {
        ObjectFactory::DeleteObject(PhysicsAssetOverride);
        PhysicsAssetOverride = nullptr;
    }

    if (PhysicsAsset)
    {
        ObjectFactory::DeleteObject(PhysicsAsset);
        PhysicsAsset = nullptr;
    }
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    // Team2AnimInstance를 사용한 상태머신 기반 애니메이션 시스템
    // AnimationBlueprint 모드로 전환
    SetAnimationMode(EAnimationMode::AnimationBlueprint);

    // Team2AnimInstance 생성 및 설정
    // UTeam2AnimInstance* Team2AnimInst = NewObject<UTeam2AnimInstance>();
    // SetAnimInstance(Team2AnimInst);

    AnimInstance = NewObject<UAnimInstance>();
    SetAnimInstance(AnimInstance);

    UAnimationStateMachine* StateMachine = NewObject<UAnimationStateMachine>();
    AnimInstance->SetStateMachine(StateMachine);

    if (AnimGraph)
    {
        FAnimBlueprintCompiler::Compile(
            AnimGraph,
            AnimInstance,
            StateMachine
        );
    }

    UE_LOG("Team2AnimInstance initialized - Idle/Walk/Run state machine ready");
    UE_LOG("Use SetMovementSpeed() to control animation transitions");
    UE_LOG("  Speed < 0.1: Idle animation");
    UE_LOG("  Speed 0.1 ~ 5.0: Walk animation");
    UE_LOG("  Speed >= 5.0: Run animation");

    // PhysicsAsset이 있으면 물리 바디 생성
    UWorld* World = GetWorld();
    if (World && World->GetPhysScene() && PhysicsAsset)
    {
        InstantiatePhysicsAssetBodies(*World->GetPhysScene());
        OnRegiDebug();
    }
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

        // P 키: PhysicsState 전환 (AnimationDriven <-> PhysicsDriven)
        if (InputManager.IsKeyPressed('P'))
        {
            if (PhysicsState == EPhysicsAnimationState::AnimationDriven)
            {
                PhysicsState = EPhysicsAnimationState::PhysicsDriven;
                UE_LOG("[SkeletalMeshComponent] PhysicsState changed to: PhysicsDriven (Ragdoll)");
            }
            else
            {
                PhysicsState = EPhysicsAnimationState::AnimationDriven;
                UE_LOG("[SkeletalMeshComponent] PhysicsState changed to: AnimationDriven");
            }
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

        // Sync physics bodies to match animation
        UWorld* World = GetWorld();
        switch (PhysicsState)
        {
            case EPhysicsAnimationState::AnimationDriven:
                AnimInstance->NativeUpdateAnimation(DeltaTime);
                SyncBodiesFromAnimation(*World->GetPhysScene());
                break;

            case EPhysicsAnimationState::PhysicsDriven:
                SyncAnimationFromBodies();
                break;

            case EPhysicsAnimationState::Blending:
                // TODO
                break;
        }

       /* GatherNotifiesFromRange(PrevAnimationTime, CurrentAnimationTime);
         
        DispatchAnimNotifies();*/
    }
    else
    {
        // 레거시 경로: AnimInstance 없이 직접 애니메이션 업데이트
        // (호환성 유지를 위해 남겨둠, 추후 제거 예정)
        TickAnimation(DeltaTime);
    }

    PrevAnimationTime = CurrentAnimationTime; 
}

void USkeletalMeshComponent::EndPlay()
{
    OnUnregiDebug();
    if (UWorld* World = GetWorld())
    {
        if (FPhysScene* PhysScene = World->GetPhysScene())
        {
            DestroyPhysicsAssetBodies(*PhysScene);
        }
    }
}

void USkeletalMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
    Super::SetSkeletalMesh(PathFileName);

    UPhysicsAsset* DefaultPhysicsAsset = SkeletalMesh ? SkeletalMesh->PhysicsAsset : nullptr;
    UPhysicsAsset* AssetToApply = DefaultPhysicsAsset;
    if (HasPhysicsAssetOverride() && PhysicsAssetOverride)
    {
        AssetToApply = PhysicsAssetOverride;
    }
    ApplyPhysicsAsset(AssetToApply);
    
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

void USkeletalMeshComponent::SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
    PhysicsAssetOverridePath.clear();
    PhysicsAssetOverride = nullptr;
    ApplyPhysicsAsset(InPhysicsAsset);
}

bool USkeletalMeshComponent::SetPhysicsAssetOverrideByPath(const FString& AssetPath)
{
    if (AssetPath.empty())
    {
        ClearPhysicsAssetOverride();
        return true;
    }

    UPhysicsAsset* LoadedAsset = UResourceManager::GetInstance().Load<UPhysicsAsset>(AssetPath);
    if (!LoadedAsset)
    {
        UE_LOG("USkeletalMeshComponent: Failed to load PhysicsAsset override from %s", AssetPath.c_str());
        return false;
    }

    PhysicsAssetOverridePath = AssetPath;
    PhysicsAssetOverride = LoadedAsset;
    ApplyPhysicsAsset(LoadedAsset);
    return true;
}

void USkeletalMeshComponent::ClearPhysicsAssetOverride()
{
    PhysicsAssetOverridePath.clear();
    PhysicsAssetOverride = nullptr;

    UPhysicsAsset* DefaultPhysicsAsset = SkeletalMesh ? SkeletalMesh->PhysicsAsset : nullptr;
    ApplyPhysicsAsset(DefaultPhysicsAsset);
}

void USkeletalMeshComponent::ApplyPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
    if (PhysicsAsset == InPhysicsAsset)
    {
        return;
    }

    UWorld* World = GetWorld();
    FPhysScene* PhysScene = World ? World->GetPhysScene() : nullptr;

    if (PhysScene && (Bodies.Num() > 0 || Constraints.Num() > 0))
    {
        DestroyPhysicsAssetBodies(*PhysScene);
    }
    else if (!PhysScene)
    {
        Bodies.Empty();
        Constraints.Empty();
    }

    PhysicsAsset = InPhysicsAsset;

    if (PhysScene && PhysicsAsset)
    {
        InstantiatePhysicsAssetBodies(*PhysScene);
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

    // 기존 로컬 스케일 보존 (PhysX는 스케일 정보가 없으므로)
    // PhysX는 스케일 없이 (1,1,1)을 반환하는데, 이걸 기반으로 행렬을 구성하면, 기존의 스케일 적용한 행렬이 깨져버리므로
    // 스케일은 원래 애니메이션포즈의 값을 가져와서 적용함
    FVector OriginalScale = CurrentLocalSpacePose[BoneIndex].Scale3D;

    FTransform DesiredLocal;
    if (ParentIndex < 0)  // 루트 본인 경우
    {
        // 컴포넌트 트랜스폼 기준으로 로컬 변환 계산
        FTransform ComponentWorldTM = GetWorldTransform();
        DesiredLocal = ComponentWorldTM.GetRelativeTransform(NewWorldTransform);
    }
    else
    {
        const FTransform& ParentWorldTransform = GetBoneWorldTransform(ParentIndex);
        DesiredLocal = ParentWorldTransform.GetRelativeTransform(NewWorldTransform);
    }

    // 스케일은 기존 값 유지 (위치/회전만 물리에서 가져옴)
    DesiredLocal.Scale3D = OriginalScale;

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

void USkeletalMeshComponent::InstantiatePhysicsAssetBodies(FPhysScene& PhysScene)
{
    // 혹시 이전에 이미 만들어진 바디/조인트가 있으면 정리
    DestroyPhysicsAssetBodies(PhysScene);

    if (!PhysicsAsset || !SkeletalMesh)
    {
        return;
    }

    // Self-Collision 방지를 위한 고유 ID 생성 (컴포넌트 포인터를 ID로 사용)
    // 같은 스켈레탈 메쉬의 모든 바디는 같은 OwnerID를 가짐
    uint32 OwnerID = static_cast<uint32>(reinterpret_cast<uintptr_t>(this) & 0xFFFFFFFF);

    // 1) BodySetups -> FBodyInstance 생성
    for (UBodySetup* Setup : PhysicsAsset->BodySetups)
    {
        if (!Setup)
            continue;

        // BoneName에 해당하는 뼈 찾기
        int32 BoneIndex = GetBoneIndexByName(Setup->BoneName);
        if (BoneIndex < 0)
        {
            continue; // 스켈레톤에 없는 본이면 스킵
        }

        // 현재 애니메이션 포즈 기준 뼈의 월드 트랜스폼
        FTransform BoneWorldTM = GetBoneWorldTransform(BoneIndex);

        // BodyInstance 생성 및 초기화
        FBodyInstance* BI = new FBodyInstance();
        BI->OwnerComponent = this;
        BI->BodySetup      = Setup;

        // Dynamic 바디로 생성 (OwnerID 전달로 Self-Collision 방지)
        BI->InitDynamic(PhysScene, BoneWorldTM, /*Mass=*/10.0f, FVector(1,1,1), OwnerID);

        Bodies.Add(BI);
    }

    // 2) Constraints -> FConstraintInstance 생성
    for (const FPhysicsConstraintSetup& CSetup : PhysicsAsset->Constraints)
    {
        // 본 이름을 기반으로 BodyInstance 찾기
        FBodyInstance* BodyA = FindBodyInstanceByName(Bodies, CSetup.BodyNameA);
        FBodyInstance* BodyB = FindBodyInstanceByName(Bodies, CSetup.BodyNameB);
        if (!BodyA || !BodyB)
        {
            continue;
        }
        
        FConstraintInstance* CI = new FConstraintInstance();
        CI->ParentBody = BodyA;
        CI->ChildBody  = BodyB;

        FConstraintLimitData Limits;
        Limits.TwistMin         = CSetup.TwistLimitMin;
        Limits.TwistMax         = CSetup.TwistLimitMax;
        Limits.Swing1           = CSetup.SwingLimitY;
        Limits.Swing2           = CSetup.SwingLimitZ;
        Limits.bEnableCollision = CSetup.bEnableCollision;
        
        // 디테일한 부분은 일단 주석처리
        //Limits.bSoftLimit = CSetup.bSoftLimit;
        //Limits.Stiffness  = CSetup.Stiffness;
        //Limits.Damping    = CSetup.Damping;

        // 로컬 프레임은 PhysicsAsset에서 미리 저장해둔 값 사용
        CI->InitD6(PhysScene, CSetup.LocalFrameA, CSetup.LocalFrameB, Limits);

        Constraints.Add(CI);
    }
}

void USkeletalMeshComponent::DestroyPhysicsAssetBodies(FPhysScene& PhysScene)
{
    // 1) 조인트 먼저 제거
    for (FConstraintInstance* CI : Constraints)
    {
        if (!CI)
            continue;

        CI->Terminate(PhysScene); // PxJoint::release() 호출
        delete CI;
    }
    Constraints.Empty();

    // 2) 바디 제거
    for (FBodyInstance* BI : Bodies)
    {
        if (!BI)
            continue;

        BI->Terminate(PhysScene); // PxRigidActor 제거 + Scene에서 remove
        delete BI;
    }
    Bodies.Empty();
}

void USkeletalMeshComponent::SyncBodiesFromAnimation(FPhysScene& PhysScene)
{
    // 애니메이션 포즈가 이미 계산되어 있다고 가정 (현재 프레임 포즈)
    // 각 BodyInstance에 대응되는 본의 월드 트랜스폼을 읽어와서
    // PhysX Actor의 글로벌 포즈로 세팅

    for (FBodyInstance* BI : Bodies)
    {
        if (!BI || !BI->BodySetup || !BI->RigidActor)
            continue;

        int32 BoneIndex = GetBoneIndexByName(BI->BodySetup->BoneName);
        if (BoneIndex < 0)
            continue;

        FTransform BoneWorldTM = GetBoneWorldTransform(BoneIndex);

        // Transform을 PhysX 포즈로 변환
        PxTransform PxPose = ToPx(BoneWorldTM);
        BI->RigidActor->setGlobalPose(PxPose);

        // Dynamic이면 속도 초기화 (초기 상태에서 튀지 않게)
        if (PxRigidDynamic* Dyn = BI->RigidActor->is<PxRigidDynamic>())
        {
            Dyn->setLinearVelocity(PxVec3(0,0,0));
            Dyn->setAngularVelocity(PxVec3(0,0,0));
        }
    }
}

void USkeletalMeshComponent::SyncAnimationFromBodies()
{
    // PhysX 시뮬레이션이 끝난 후 (PhysScene.StepSimulation 뒤)
    // 각 BodyInstance에서 월드 트랜스폼을 가져와서
    // 해당 본의 월드 트랜스폼으로 덮어쓴다.

    UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
    if (!World || !World->GetPhysScene())
        return;

    PxScene* PxScenePtr = World->GetPhysScene()->GetScene();
    if (!PxScenePtr)
        return;

    // Thread-Safe: 물리 데이터 읽기 시 Lock 획득
    SCOPED_PHYSX_READ_LOCK(*PxScenePtr);

    for (FBodyInstance* BI : Bodies)
    {
        if (!BI || !BI->BodySetup)
            continue;

        int32 BoneIndex = GetBoneIndexByName(BI->BodySetup->BoneName);
        if (BoneIndex < 0)
            continue;

        FTransform WorldTM = BI->GetWorldTransform();

        // 여기서 선택지:
        // 1) 바로 월드 트랜스폼으로 세팅하는 함수가 있으면 그대로 사용
        // 2) 부모 본의 월드 트랜스폼을 이용해 로컬로 변환해서
        //    스켈레톤 로컬 포즈 배열에 넣는 방식

        SetBoneWorldTransform(BoneIndex, WorldTM);
    }

    // 이후에 스킨 메쉬 업데이트 (변형된 본 행렬로 버텍스 스키닝)
}

int32 USkeletalMeshComponent::GetBoneIndexByName(const FName& BoneName) const
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshData())
    {
        return -1;
    }
    
    const FSkeleton& Skeleton = SkeletalMesh->GetSkeletalMeshData()->Skeleton;
    return Skeleton.FindBoneIndex(BoneName);
}

void USkeletalMeshComponent::OnRegiDebug()
{
    TestContactHit = GetWorld()->GetPhysScene()->OnContactDelegate.AddDynamic(this, &USkeletalMeshComponent::GameLogicTest);
}

void USkeletalMeshComponent::OnUnregiDebug()
{
    if (TestContactHit && GetWorld() && GetWorld()->GetPhysScene())
    {
        GetWorld()->GetPhysScene()->OnContactDelegate.Remove(TestContactHit);
    }
}

void USkeletalMeshComponent::GameLogicTest(FContactHit ContactHit)
{
    // PhysicsState = EPhysicsAnimationState::PhysicsDriven;
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

    float OldTime = CurrentAnimationTime;
    CurrentAnimationTime = InTime;

    if (CurrentAnimation)
    {
        float PlayLength = CurrentAnimation->GetPlayLength();

        // 루핑 처리
        bool bDidWrap = false;
        if (bIsLooping)
        {
            while (CurrentAnimationTime < 0.0f)
            {
                CurrentAnimationTime += PlayLength;
                bDidWrap = true;
            }
            while (CurrentAnimationTime > PlayLength)
            {
                CurrentAnimationTime -= PlayLength;
                bDidWrap = true;
            }
        }
        else
        {
            // 범위 제한
            CurrentAnimationTime = FMath::Clamp(CurrentAnimationTime, 0.0f, PlayLength);
        }

        // 노티파이 수집 및 실행
        // 루프 경계를 넘어가는 경우 처리
        if (bDidWrap && bIsLooping && PlayLength > 0.0f)
        {
            // 루프가 발생한 경우: 두 구간으로 나눠서 노티파이 수집
            // 구간 1: PrevTime → PlayLength
            PendingNotifies.Empty();
            float DeltaToEnd = PlayLength - PrevAnimationTime;
            CurrentAnimation->GetAnimNotify(PrevAnimationTime, DeltaToEnd, PendingNotifies);
            DispatchAnimNotifies();

            // 구간 2: 0 → CurrentTime
            if (CurrentAnimationTime > 0.0f)
            {
                PendingNotifies.Empty();
                CurrentAnimation->GetAnimNotify(0.0f, CurrentAnimationTime, PendingNotifies);
                DispatchAnimNotifies();
            }
        }
        else
        {
            // 일반적인 경우
            GatherNotifiesFromRange(PrevAnimationTime, CurrentAnimationTime);
            DispatchAnimNotifies();
        }

        PrevAnimationTime = CurrentAnimationTime;

        // 포즈 평가 및 적용
        UAnimDataModel* DataModel = CurrentAnimation->GetDataModel();
        if (DataModel && SkeletalMesh)
        {
            const FSkeleton& Skeleton = SkeletalMesh->GetSkeletalMeshData()->Skeleton;
            int32 NumBones = DataModel->GetNumBoneTracks();

            FAnimExtractContext ExtractContext(CurrentAnimationTime, bIsLooping);
            FPoseContext PoseContext(NumBones);
            CurrentAnimation->GetAnimationPose(PoseContext, ExtractContext);

            // 추출된 포즈를 CurrentLocalSpacePose에 적용
            const TArray<FBoneAnimationTrack>& BoneTracks = DataModel->GetBoneAnimationTracks();
            for (int32 TrackIdx = 0; TrackIdx < BoneTracks.Num(); ++TrackIdx)
            {
                const FBoneAnimationTrack& Track = BoneTracks[TrackIdx];
                int32 BoneIndex = Skeleton.FindBoneIndex(Track.Name);

                if (BoneIndex != INDEX_NONE && BoneIndex < CurrentLocalSpacePose.Num())
                {
                    CurrentLocalSpacePose[BoneIndex] = PoseContext.Pose[TrackIdx];
                }
            }

            // 포즈 변경 사항을 스키닝에 반영
            ForceRecomputePose();
        }
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

void USkeletalMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // Read AnimGraphPath explicitly (bypass codegen dependency)
        FString LoadedGraphPath;
        FJsonSerializer::ReadString(InOutHandle, "AnimGraphPath", LoadedGraphPath, "", false);
        if (!LoadedGraphPath.empty())
        {
            AnimGraphPath = LoadedGraphPath;
        }

        // Base (USkinnedMeshComponent) already restores SkeletalMesh; ensure internal state is initialized
        if (SkeletalMesh)
        {
            SetSkeletalMesh(SkeletalMesh->GetPathFileName());
        }

        // Load animation graph from saved path if available
        if (!AnimGraphPath.empty())
        {
            JSON GraphJson;
            if (FJsonSerializer::LoadJsonFromFile(GraphJson, UTF8ToWide(AnimGraphPath)))
            {
                UAnimationGraph* NewGraph = NewObject<UAnimationGraph>();
                NewGraph->Serialize(true, GraphJson);
                SetAnimGraph(NewGraph);
            }
        }
        FString LoadedPhysicsOverride;
        FJsonSerializer::ReadString(InOutHandle, "PhysicsAssetOverridePath", LoadedPhysicsOverride, "", false);
        if (!LoadedPhysicsOverride.empty())
        {
            SetPhysicsAssetOverrideByPath(LoadedPhysicsOverride);
        }
        else
        {
            ClearPhysicsAssetOverride();
        }
    }
    else
    {
        // Persist AnimGraphPath explicitly to ensure presence in prefab JSON
        if (!AnimGraphPath.empty())
        {
            InOutHandle["AnimGraphPath"] = AnimGraphPath.c_str();
        }
        else
        {
            // Ensure key exists for clarity (optional)
            InOutHandle["AnimGraphPath"] = "";
        }
        if (!PhysicsAssetOverridePath.empty())
        {
            InOutHandle["PhysicsAssetOverridePath"] = PhysicsAssetOverridePath.c_str();
        }
        else
        {
            InOutHandle["PhysicsAssetOverridePath"] = "";
        }
    }
}
