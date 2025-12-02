#pragma once
#include "SkinnedMeshComponent.h"
#include "USkeletalMeshComponent.generated.h"
// Include for FPendingAnimNotify and FAnimNotifyEvent types
#include "Source/Runtime/Engine/Animation/AnimTypes.h"
class UAnimationGraph;
class UAnimationAsset;
class UAnimSequence;
class UAnimInstance;
class FBodyInstance;
class FConstraintInstance;
class FPhysScene;
class UPhysicsAsset;
struct FPendingAnimNotify;

enum class EPhysicsAnimationState : uint8
{
    AnimationDriven, // Collider(BodyInstance)가 완전히 Bone을 따라가는 상황, Kinematic
    PhysicsDriven, // Bone이 완전히 Collider(BodyInstance)를 따라가는 상황, Dynamic
    Blending // 위 두 상태가 전환되거나, 부분 래그돌 등에 사용, Linear하게 Kinematic + Dynamic
};

UCLASS(DisplayName="스켈레탈 메시 컴포넌트", Description="스켈레탈 메시를 렌더링하는 컴포넌트입니다")
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    GENERATED_REFLECTION_BODY()
    
    USkeletalMeshComponent();
    ~USkeletalMeshComponent();

public:
    void BeginPlay() override;
    void TickComponent(float DeltaTime) override;
    void EndPlay() override;

    // Serialize to persist AnimGraphPath and reuse base behavior
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

public:
    void SetSkeletalMesh(const FString& PathFileName) override;

    FAABB GetWorldAABB() const override;

    /////////////////////////////////////////////////////////////
    // Animation Section
    /////////////////////////////////////////////////////////////
    void SetAnimationMode(EAnimationMode InAnimationMode);

    /**
     * @brief 재생할 애니메이션 설정
     * @param InAnimation 재생할 UAnimSequence
     */
    void SetAnimation(UAnimSequence* InAnimation);

    /**
     * @brief 현재 재생 중인 애니메이션 가져오기
     */
    UAnimSequence* GetAnimation() const { return CurrentAnimation; }

    /**
     * @brief 애니메이션 그래프 가져오기
     */
    UAnimationGraph* GetAnimGraph() const { return AnimGraph; }

    /**
     * @brief 애니메이션 그래프 설정 
     */
    void SetAnimGraph(UAnimationGraph* InAnimGraph) 
    { 
        AnimGraph = InAnimGraph;
    }

    /**
     * @brief 애니메이션 재생 시작/중지
     * @param bInPlaying true면 재생, false면 일시정지
     */
    void SetPlaying(bool bInPlaying) { bIsPlaying = bInPlaying; }

    /**
     * @brief 애니메이션 재생 중인지 확인
     */
    bool IsPlaying() const { return bIsPlaying; }

    /**
     * @brief 애니메이션 루핑 설정
     * @param bInLooping true면 반복 재생
     */
    void SetLooping(bool bInLooping) { bIsLooping = bInLooping; }

    /**
     * @brief 애니메이션이 루핑 중인지 확인
     */
    bool IsLooping() const { return bIsLooping; }

    /**
     * @brief 현재 애니메이션 재생 시간 설정
     * @param InTime 설정할 시간 (초)
     */
    void SetAnimationTime(float InTime);

    /**
     * @brief 현재 애니메이션 재생 시간 가져오기
     */
    float GetAnimationTime() const { return CurrentAnimationTime; }

    /**
     * @brief 애니메이션 재생 속도 설정
     * @param InPlayRate 재생 속도 (1.0 = 정상 속도, 2.0 = 2배속)
     */
    void SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; }

    /**
     * @brief 애니메이션 재생 속도 가져오기
     */
    float GetPlayRate() const { return PlayRate; }

    /**
     * @brief UE 스타일 애니메이션 재생 API
     * @param NewAnimToPlay 재생할 애니메이션 에셋
     * @param bLooping 루프 여부
     *
     * AnimInstance를 비활성화하고 단일 애니메이션을 직접 재생합니다.
     * (SingleNode Animation Mode 흉내)
     */
    void PlayAnimation(class UAnimationAsset* NewAnimToPlay, bool bLooping);

    /**
     * @brief 현재 재생 중인 애니메이션 정지
     */
    void StopAnimation();

    /**
    * @brief Notifies들을 수집하는 함수
    */
    void GatherNotifies(float DeltaTime);
    void GatherNotifiesFromRange(float PrevTime, float CurTime);

    void DispatchAnimNotifies();

    /**
     * @brief AnimInstance에서 계산한 포즈를 컴포넌트에 적용
     * @param InPose 적용할 포즈 (본별 로컬 트랜스폼)
     */
    void SetAnimationPose(const TArray<FTransform>& InPose);

    /**
     * @brief AnimInstance 가져오기
     */
    UAnimInstance* GetAnimInstance() const { return AnimInstance; }

    /**
     * @brief AnimInstance 설정 (선택적)
     */
    void SetAnimInstance(UAnimInstance* InAnimInstance);

    FString GetAnimGraphPath() { return AnimGraphPath; }
    void SetAnimGraphPath(FString InAnimGraphPath) { AnimGraphPath = InAnimGraphPath; }

    UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }
	void SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset);
	bool SetPhysicsAssetOverrideByPath(const FString& AssetPath);
	void ClearPhysicsAssetOverride();
	bool HasPhysicsAssetOverride() const { return !PhysicsAssetOverridePath.empty(); }
	const FString& GetPhysicsAssetOverridePath() const { return PhysicsAssetOverridePath; }

protected:
    /**
     * @brief 애니메이션 업데이트 (TickComponent에서 호출)
     */
    void TickAnimation(float DeltaTime);

    /**
     * @brief 애니메이션이 업데이트되어야 하는지 확인
     */
    bool ShouldTickAnimation() const;

    /**
     * @brief 애니메이션 포즈를 적용
     */
    void TickAnimInstances(float DeltaTime);

protected:
    /** 현재 재생 중인 애니메이션 */
    UPROPERTY()
    UAnimSequence* CurrentAnimation = nullptr;

    /** 애니메이션 재생 중인지 여부 */
    bool bIsPlaying = false;

    /** 애니메이션 루핑 여부 */
    bool bIsLooping = false;

    /** 현재 애니메이션 재생 시간 (초) */
    float CurrentAnimationTime = 0.0f;
     
    /** 이전 애니메이션 재생 시간 (초) */
    float PrevAnimationTime = 0.0f;

    /** 애니메이션 재생 속도 */
    float PlayRate = 1.0f;

    /** 애니메이션 인스턴스 (향후 확장) */
    UPROPERTY()
    UAnimInstance* AnimInstance = nullptr;

    EAnimationMode AnimationMode = EAnimationMode::AnimationBlueprint;

    /** 애니메이션 블루프린트 */
    UAnimationGraph* AnimGraph = nullptr;

    // Animation graph asset path (.graph). Saved into prefab and used to reload graph.
    UPROPERTY(EditAnywhere, Category = "Animation")
    FString AnimGraphPath;
      
// Editor Section
public:
    /**
     * @brief 특정 뼈의 부모 기준 로컬 트랜스폼을 설정
     * @param BoneIndex 수정할 뼈의 인덱스
     * @param NewLocalTransform 새로운 부모 기준 로컬 FTransform
     */
    void SetBoneLocalTransform(int32 BoneIndex, const FTransform& NewLocalTransform);

    void SetBoneWorldTransform(int32 BoneIndex, const FTransform& NewWorldTransform);
    
    /**
     * @brief 특정 뼈의 현재 로컬 트랜스폼을 반환
     */
    FTransform GetBoneLocalTransform(int32 BoneIndex) const;
    
    /**
     * @brief 기즈모를 렌더링하기 위해 특정 뼈의 월드 트랜스폼을 계산
     */
    FTransform GetBoneWorldTransform(int32 BoneIndex);

protected:
    /**
     * @brief CurrentLocalSpacePose의 변경사항을 ComponentSpace -> FinalMatrices 계산까지 모두 수행
     */
    void ForceRecomputePose();

    /**
     * @brief CurrentLocalSpacePose를 기반으로 CurrentComponentSpacePose 채우기
     */
    void UpdateComponentSpaceTransforms();

    /**
     * @brief CurrentComponentSpacePose를 기반으로 TempFinalSkinningMatrices 채우기
     */
    void UpdateFinalSkinningMatrices();

protected:
    /**
     * @brief 각 뼈의 부모 기준 로컬 트랜스폼
     */
    TArray<FTransform> CurrentLocalSpacePose;

    /**
     * @brief LocalSpacePose로부터 계산된 컴포넌트 기준 트랜스폼
     */
    TArray<FTransform> CurrentComponentSpacePose;

    /**
     * @brief 부모에게 보낼 최종 스키닝 행렬 (임시 계산용)
     */
    TArray<FMatrix> TempFinalSkinningMatrices;
    /**
     * @brief CPU 스키닝에 전달할 최종 노말 스키닝 행렬
     */
    TArray<FMatrix> TempFinalSkinningNormalMatrices;

    /**
    * @brief Notifies들을 한 번에 처리하기 위한 행렬
    */
    TArray<FPendingAnimNotify> PendingNotifies;

    /////////////////////////////////////////////////////////////
    // Physics Section
    /////////////////////////////////////////////////////////////

public:
    void InstantiatePhysicsAssetBodies(FPhysScene& PhysScene);
    void DestroyPhysicsAssetBodies(FPhysScene& PhysScene);

    void SyncBodiesFromAnimation(FPhysScene& PhysScene); // 애님 포즈 → 바디 초기화 (ragdoll 전)
    void SyncAnimationFromBodies();                      // 래그돌 포즈 → 본 트랜스폼 반영

    int32 GetBoneIndexByName(const FName& BoneName) const;

private:
    UPhysicsAsset*   PhysicsAsset = nullptr;   // 이 메쉬에 쓸 물리 에셋 (콜라이더/조인트 정의)
    UPhysicsAsset*   PhysicsAssetOverride = nullptr; // Instance-level override asset (if any)
    FString          PhysicsAssetOverridePath;

    void ApplyPhysicsAsset(UPhysicsAsset* InPhysicsAsset);    
    TArray<FBodyInstance*>       Bodies;       // 각 본(혹은 일부 본)에 대응하는 물리 바디 인스턴스
    TArray<FConstraintInstance*> Constraints;  // 바디들 사이를 묶는 조인트 인스턴스

    /////////////////////////////////////////////////////////////
    // Ragdoll Section
    /////////////////////////////////////////////////////////////
private:
    EPhysicsAnimationState PhysicsState = EPhysicsAnimationState::AnimationDriven;
    // EPhysicsAnimationState PhysicsState = EPhysicsAnimationState::PhysicsDriven;
    float BlendWeight;
    float BlendTime;

public:
    void SetPhysicsAnimationState(EPhysicsAnimationState NewState, float InBlendTime = 0.2f) { PhysicsState = NewState; BlendTime = InBlendTime; };

    // Event Call Back Test
    void OnRegiDebug();
    void OnUnregiDebug();
    void GameLogicTest(FContactHit ContactHit);

    FDelegateHandle TestContactHit{};
};
