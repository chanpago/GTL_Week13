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
    , ProbeSize(12.0f)
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

    // 매 프레임 암 위치 업데이트
    UpdateDesiredArmLocation();

    // 자식 컴포넌트들(카메라 등)의 위치를 암 끝으로 업데이트
    FVector SocketLocation = GetSocketLocalLocation();
    for (USceneComponent* Child : GetAttachChildren())
    {
        if (Child)
        {
            Child->SetRelativeLocation(SocketLocation);
        }
    }
}

void USpringArmComponent::UpdateDesiredArmLocation()
{
    // 암의 시작점 (타겟 위치 + 오프셋)
    FVector Origin = GetWorldLocation() + TargetOffset;

    // 암의 방향 계산 (뒤쪽으로 뻗음, -X 방향이 뒤)
    // SpringArm의 회전을 기준으로 뒤쪽 방향 계산
    FQuat ArmRotation = GetWorldRotation();
    FVector BackwardDir = ArmRotation.RotateVector(FVector(-1, 0, 0));

    // 목표 암 끝 위치
    FVector DesiredEnd = Origin + BackwardDir * TargetArmLength;

    // 충돌 체크로 실제 암 길이 계산
    if (bDoCollisionTest)
    {
        CurrentArmLength = CalculateArmLengthWithCollision(Origin, DesiredEnd);
    }
    else
    {
        CurrentArmLength = TargetArmLength;
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
