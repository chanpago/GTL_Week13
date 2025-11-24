#include "pch.h"
#include "K2Node_Expression.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintEvaluator.h"

// ----------------------------------------------------------------
//  Select 평가 헬퍼 함수 (템플릿)
// ----------------------------------------------------------------
// @note: Condition을 먼저 확인하고, 선택된 핀만 EvaluateInput을 호출함으로써
// 불필요한 연산을 방지한다. (Short-circuit Evaluation).
template <typename T>
FBlueprintValue EvaluateSelectOp(const UEdGraphNode* Node, FBlueprintContext* Context)
{
    // 1. Condition 핀 평가
    bool bCondition = FBlueprintEvaluator::EvaluateInput<bool>(Node->FindPin("Condition"), Context);

    // 2. 조건에 따라 평가할 핀 결정
    const char* PinToEvaluate = bCondition ? "TrueValue" : "FalseValue";
    UEdGraphPin* TargetPin = Node->FindPin(PinToEvaluate);

    // 3. 선택된 핀의 값만 가져오기 (Pull Model)
    T ResultValue = FBlueprintEvaluator::EvaluateInput<T>(TargetPin, Context);

    return FBlueprintValue(ResultValue);
}

// ----------------------------------------------------------------
//	[Int] Select 노드 구현
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Select_Int)

UK2Node_Select_Int::UK2Node_Select_Int()
{
    TitleColor = ImColor(68, 201, 156); // Int 색상 유지
}

void UK2Node_Select_Int::AllocateDefaultPins()
{
    // Condition은 맨 위에
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "Condition");
    
    // 선택지 A, B
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "TrueValue", "0");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "FalseValue", "0");
    
    // 결과
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Int, "Result");
}

void UK2Node_Select_Int::RenderBody()
{
    // 순수 노드이므로 별도의 ImGui 바디 렌더링 없음
}

FBlueprintValue UK2Node_Select_Int::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateSelectOp<int32>(this, Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Select_Int::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Float] Select 노드 구현
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Select_Float)

UK2Node_Select_Float::UK2Node_Select_Float()
{
    TitleColor = ImColor(147, 226, 74); // Float 색상 유지
}

void UK2Node_Select_Float::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "Condition");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "TrueValue", "0.0");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "FalseValue", "0.0");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Result");
}

void UK2Node_Select_Float::RenderBody()
{
}

FBlueprintValue UK2Node_Select_Float::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateSelectOp<float>(this, Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Select_Float::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Bool] Select 노드 구현
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Select_Bool)

UK2Node_Select_Bool::UK2Node_Select_Bool()
{
    TitleColor = ImColor(220, 48, 48); // Bool 색상 유지
}

void UK2Node_Select_Bool::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "Condition");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "TrueValue", "true");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "FalseValue", "false");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Select_Bool::RenderBody()
{
}

FBlueprintValue UK2Node_Select_Bool::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateSelectOp<bool>(this, Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Select_Bool::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}