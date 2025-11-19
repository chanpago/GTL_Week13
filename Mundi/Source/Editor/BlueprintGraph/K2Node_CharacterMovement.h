/**
* ===========================================================================
 * @file      K2Node_CharacterMovement.h
 * @author    geb0598
 * @date      2025/11/19
 * @brief     캐릭터 움직임과 관련된 블루프린트 노드를 정의한다.
 *
 * ===========================================================================
 *
 * @note 현재 Context는 UAnimInstance타입이라고 가정한다. 따라서, UAnimInstance에서
 *       출발하여 원하는 컴포넌트를 찾을 때까지 계층을 돌며 탐색한다.
 *
 * ===========================================================================
 */

#pragma once

#include "K2Node.h"

// ----------------------------------------------------------------
//	[GetIsFalling] 캐릭터 낙하 상태 확인 노드
// ----------------------------------------------------------------
UCLASS(DisplayName = "Is Falling", Description = "캐릭터가 현재 공중에 떠있는지(낙하 중인지) 확인합니다.")
class UK2Node_GetIsFalling : public UK2Node
{
    DECLARE_CLASS(UK2Node_GetIsFalling, UK2Node);

public:
    UK2Node_GetIsFalling();

    // --- UEdGraphNode 인터페이스 ---
public:
    virtual FString GetNodeTitle() const override { return "Is Falling"; }
    virtual bool IsNodePure() const override { return true; }
    virtual void AllocateDefaultPins() override;
    virtual FBlueprintValue EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context) override;

    // --- UK2Node 인터페이스 ---
public:
    virtual FString GetMenuCategory() const override { return "캐릭터 무브먼트"; };
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};

// ----------------------------------------------------------------
//	[GetVelocity] 이동 속도 벡터 반환 노드
// ----------------------------------------------------------------
UCLASS(DisplayName = "Get Velocity", Description = "캐릭터의 현재 이동 속도 벡터(X, Y, Z)를 반환합니다.")
class UK2Node_GetVelocity : public UK2Node
{
    DECLARE_CLASS(UK2Node_GetVelocity, UK2Node);

public:
    UK2Node_GetVelocity();

    // --- UEdGraphNode 인터페이스 ---
public:
    virtual FString GetNodeTitle() const override { return "Get Velocity"; }
    virtual bool IsNodePure() const override { return true; }
    virtual void AllocateDefaultPins() override;
    virtual FBlueprintValue EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context) override;

    // --- UK2Node 인터페이스 ---
public:
    virtual FString GetMenuCategory() const override { return "캐릭터 무브먼트"; };
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};

