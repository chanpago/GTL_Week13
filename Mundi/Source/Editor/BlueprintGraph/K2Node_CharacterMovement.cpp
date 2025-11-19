#include "pch.h"
#include "K2Node_CharacterMovement.h"

#include "BlueprintActionDatabase.h"
#include "CharacterMovementComponent.h"
#include "SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"

// ----------------------------------------------------------------
//	Internal Helper: 컨텍스트에서 MovementComponent 탐색
// ----------------------------------------------------------------
static UCharacterMovementComponent* GetMovementFromContext(FBlueprintContext* Context)
{
    if (!Context || !Context->SourceObject)
    {
        return nullptr;
    }

    UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context->SourceObject);
    if (!AnimInstance)
    {
        return nullptr;
    }

    USkeletalMeshComponent* MeshComp = AnimInstance->GetOwningComponent();
    if (!MeshComp)
    {
        return nullptr;
    }

    auto* OwnerActor = MeshComp->GetOwner();
    if (!OwnerActor)
    {
        return nullptr;
    }

    UCharacterMovementComponent* MoveComp = Cast<UCharacterMovementComponent>(
        OwnerActor->GetComponent(UCharacterMovementComponent::StaticClass())
    );

    return MoveComp;
}

// ----------------------------------------------------------------
//	[GetIsFalling] 
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_GetIsFalling, UK2Node)

UK2Node_GetIsFalling::UK2Node_GetIsFalling()
{
    TitleColor = ImColor(100, 200, 100); // Pure Node Green
}

void UK2Node_GetIsFalling::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Is Falling");
}

FBlueprintValue UK2Node_GetIsFalling::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    UCharacterMovementComponent* MoveComp = GetMovementFromContext(Context);

    if (OutputPin->PinName == "Is Falling")
    {
        if (!MoveComp)
        {
            return FBlueprintValue(false); 
        }
        
        return FBlueprintValue(MoveComp->IsFalling());
    }

    return FBlueprintValue{};
}

void UK2Node_GetIsFalling::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[GetVelocity] 
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_GetVelocity, UK2Node)

UK2Node_GetVelocity::UK2Node_GetVelocity()
{
    TitleColor = ImColor(100, 200, 100);
}

void UK2Node_GetVelocity::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "X");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Y");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Z");
}

FBlueprintValue UK2Node_GetVelocity::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    auto* MoveComp = GetMovementFromContext(Context);

    if (!MoveComp)
    {
        return FBlueprintValue(0.0f);
    }

    FVector Velocity = MoveComp->GetVelocity();

    if (OutputPin->PinName == "X")
    {
        return FBlueprintValue(Velocity.X);
    }
    else if (OutputPin->PinName == "Y")
    {
        return FBlueprintValue(Velocity.Y);
    }
    else if (OutputPin->PinName == "Z")
    {
        return FBlueprintValue(Velocity.Z);
    }

    return FBlueprintValue(0.0f);
}

void UK2Node_GetVelocity::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}