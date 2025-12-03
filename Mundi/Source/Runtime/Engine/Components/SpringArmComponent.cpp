#include "pch.h"
#include "SpringArmComponent.h"
#include "World.h"
#include "Source/Runtime/Engine/Collision/Collision.h"
#include "Source/Runtime/Engine/Physics/PhysScene.h"

USpringArmComponent::USpringArmComponent()
    : TargetArmLength(300.0f)
    , TargetOffset(FVector::Zero())
    , SocketOffset(FVector::Zero())
    , bDoCollisionTest(true)
    , ProbeSize(1.0f)
    , bUsePawnControlRotation(true)
    , CurrentArmLength(300.0f)
{
    // TickComponent가 호출되도록 설정
    bCanEverTick = true;
}

USpringArmComponent::~USpringArmComponent()
{
}

void USpringArmComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);

    // 초기 암 길이 설정
    CurrentArmLength = TargetArmLength;
}

void USpringArmComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // 매 프레임 암 위치 업데이트 (보간 포함)
    UpdateDesiredArmLocation(DeltaTime);

    // 자식 컴포넌트(카메라)의 로컬 위치를 암 끝으로 설정
    // SpringArm의 로컬 좌표계에서 -X 방향(뒤쪽)으로 CurrentArmLength만큼
    FVector SocketLocalLocation = FVector(-CurrentArmLength, 0, 0) + SocketOffset;

    for (USceneComponent* Child : GetAttachChildren())
    {
        if (Child)
        {
            Child->SetRelativeLocation(SocketLocalLocation);
        }
    }
}

void USpringArmComponent::UpdateDesiredArmLocation(float DeltaTime)
{
    // SpringArm의 월드 위치 (피벗 포인트)에서 카메라 방향으로 레이 발사
    FVector Origin = GetWorldLocation() + TargetOffset;

    // 암의 방향 계산 (뒤쪽으로 뻗음, -X 방향이 뒤)
    // SpringArm의 회전을 기준으로 뒤쪽 방향 계산
    FQuat ArmRotation = GetWorldRotation();
    FVector BackwardDir = ArmRotation.RotateVector(FVector(-1, 0, 0));

    // 목표 암 끝 위치
    FVector DesiredEnd = Origin + BackwardDir * TargetArmLength;

    // 충돌 체크로 목표 암 길이 계산
    float DesiredArmLength = TargetArmLength;
    if (bDoCollisionTest)
    {
        DesiredArmLength = CalculateArmLengthWithCollision(Origin, DesiredEnd);
    }

    // 부드러운 보간으로 CurrentArmLength 업데이트
    // 충돌 시(줄어들 때)는 빠르게, 복귀 시(늘어날 때)는 느리게
    const float ShrinkSpeed = 15.0f;  // 충돌 시 줄어드는 속도
    const float GrowSpeed = 5.0f;     // 복귀 시 늘어나는 속도

    if (DesiredArmLength < CurrentArmLength)
    {
        // 충돌로 인해 줄어들어야 함 - 빠르게
        float Alpha = FMath::Clamp(DeltaTime * ShrinkSpeed, 0.0f, 1.0f);
        CurrentArmLength = FMath::Lerp(CurrentArmLength, DesiredArmLength, Alpha);
    }
    else
    {
        // 충돌 없어서 원래 길이로 복귀 - 느리게
        float Alpha = FMath::Clamp(DeltaTime * GrowSpeed, 0.0f, 1.0f);
        CurrentArmLength = FMath::Lerp(CurrentArmLength, DesiredArmLength, Alpha);
    }
}

float USpringArmComponent::CalculateArmLengthWithCollision(const FVector& Origin, const FVector& DesiredEnd)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return TargetArmLength;
    }

    FPhysScene* PhysScene = World->GetPhysScene();
    if (!PhysScene)
    {
        return TargetArmLength;
    }

    // Owner Actor 가져오기 (충돌 무시용)
    AActor* OwnerActor = GetOwner();

    // SweepSphere로 충돌 체크
    FHitResult HitResult;
    bool bHit = PhysScene->SweepSphere(
        Origin,
        DesiredEnd,
        ProbeSize,
        HitResult,
        OwnerActor
    );

    if (bHit && HitResult.bBlockingHit)
    {
        // 충돌 시 암 길이를 충돌 지점까지로 줄임
        // HitResult.Time은 0~1 사이 값으로, 얼마나 진행했는지 나타냄
        float HitArmLength = TargetArmLength * HitResult.Time;

        // 최소 거리 보장 (너무 가까워지지 않도록)
        const float MinArmLength = ProbeSize * 2.0f;
        return FMath::Max(HitArmLength, MinArmLength);
    }

    return TargetArmLength;
}

FVector USpringArmComponent::GetSocketLocalLocation() const
{
    // 로컬 좌표계에서 암 끝 위치 계산
    // -X 방향(뒤쪽)으로 CurrentArmLength만큼 + SocketOffset
    return FVector(-CurrentArmLength, 0, 0) + SocketOffset;
}

void USpringArmComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadFloat(InOutHandle, "TargetArmLength", TargetArmLength, 300.0f);
        FJsonSerializer::ReadVector(InOutHandle, "TargetOffset", TargetOffset, FVector::Zero());
        FJsonSerializer::ReadVector(InOutHandle, "SocketOffset", SocketOffset, FVector::Zero());
        FJsonSerializer::ReadBool(InOutHandle, "bDoCollisionTest", bDoCollisionTest, true);
        FJsonSerializer::ReadFloat(InOutHandle, "ProbeSize", ProbeSize, 12.0f);
        FJsonSerializer::ReadBool(InOutHandle, "bUsePawnControlRotation", bUsePawnControlRotation, true);
    }
    else
    {
        InOutHandle["TargetArmLength"] = TargetArmLength;
        InOutHandle["TargetOffset"] = FJsonSerializer::VectorToJson(TargetOffset);
        InOutHandle["SocketOffset"] = FJsonSerializer::VectorToJson(SocketOffset);
        InOutHandle["bDoCollisionTest"] = bDoCollisionTest;
        InOutHandle["ProbeSize"] = ProbeSize;
        InOutHandle["bUsePawnControlRotation"] = bUsePawnControlRotation;
    }
}
