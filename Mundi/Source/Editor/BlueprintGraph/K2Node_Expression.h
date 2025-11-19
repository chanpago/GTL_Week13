/**
* ===========================================================================
 * @file      K2Node_Expression.h
 * @author    geb0598 
 * @date      2025/11/19
 * @brief     표현식과 관련된 노드들을 정의한다.
 *
 * ===========================================================================
 * @note 코딩 규칙
 * ===========================================================================
 */

#pragma once

#include "K2Node.h"

// ----------------------------------------------------------------
//	Select 노드
// ----------------------------------------------------------------
// @note 구현 원리 (Ternary Operator)
// - 입력: Condition (Bool), TrueValue (T), FalseValue (T)
// - 출력: Result (T)
// - 동작: Condition이 True면 TrueValue를, False면 FalseValue를 반환한다.
// - 특징: Short-circuit evaluation을 지원하여 선택되지 않은 핀은 평가하지 않는다.
// ----------------------------------------------------------------

UCLASS(DisplayName = "UK2Node_Select_Int", Description = "Condition에 따라 두 Int 값 중 하나를 선택합니다.")
class UK2Node_Select_Int : public UK2Node
{
    DECLARE_CLASS(UK2Node_Select_Int, UK2Node);

public:
    UK2Node_Select_Int();

    // --- UEdGraphNode 인터페이스 ---
public:
    virtual FString GetNodeTitle() const override { return "Select (Int)"; }
    virtual bool IsNodePure() const override { return true; }
    virtual void AllocateDefaultPins() override;
    virtual void RenderBody() override;
    virtual FBlueprintValue EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context) override;

    // --- UK2Node 인터페이스 ---
public:
    virtual FString GetMenuCategory() const override { return "표현식"; }
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};

UCLASS(DisplayName = "UK2Node_Select_Float", Description = "Condition에 따라 두 Float 값 중 하나를 선택합니다.")
class UK2Node_Select_Float : public UK2Node
{
    DECLARE_CLASS(UK2Node_Select_Float, UK2Node);

public:
    UK2Node_Select_Float();

    // --- UEdGraphNode 인터페이스 ---
public:
    virtual FString GetNodeTitle() const override { return "Select (Float)"; }
    virtual bool IsNodePure() const override { return true; }
    virtual void AllocateDefaultPins() override;
    virtual void RenderBody() override;
    virtual FBlueprintValue EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context) override;

    // --- UK2Node 인터페이스 ---
public:
    virtual FString GetMenuCategory() const override { return "표현식"; }
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};

UCLASS(DisplayName = "UK2Node_Select_Bool", Description = "Condition에 따라 두 Bool 값 중 하나를 선택합니다.")
class UK2Node_Select_Bool : public UK2Node
{
    DECLARE_CLASS(UK2Node_Select_Bool, UK2Node);

public:
    UK2Node_Select_Bool();

    // --- UEdGraphNode 인터페이스 ---
public:
    virtual FString GetNodeTitle() const override { return "Select (Bool)"; }
    virtual bool IsNodePure() const override { return true; }
    virtual void AllocateDefaultPins() override;
    virtual void RenderBody() override;
    virtual FBlueprintValue EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context) override;

    // --- UK2Node 인터페이스 ---
public:
    virtual FString GetMenuCategory() const override { return "표현식"; }
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};