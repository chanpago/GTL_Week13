#include "pch.h"
#include "K2Node_Math.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintEvaluator.h"

// ----------------------------------------------------------------
//  표현식 평가(Expression Evaluation)용 헬퍼함수
// ----------------------------------------------------------------

template <typename T, typename OpFunc>
FBlueprintValue EvaluateUnaryOp(const UEdGraphNode* Node, OpFunc Op, FBlueprintContext* Context)
{
    T A = FBlueprintEvaluator::EvaluateInput<T>(Node->FindPin("A"), Context);
    return FBlueprintValue(Op(A));
}

template <typename T, typename OpFunc>
FBlueprintValue EvaluateBinaryOp(const UEdGraphNode* Node, OpFunc Op, FBlueprintContext* Context)
{
    T A = FBlueprintEvaluator::EvaluateInput<T>(Node->FindPin("A"), Context);
    T B = FBlueprintEvaluator::EvaluateInput<T>(Node->FindPin("B"), Context);
    
    return FBlueprintValue(Op(A, B)); 
}

// ----------------------------------------------------------------
//  [Float] 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Add_FloatFloat)

UK2Node_Add_FloatFloat::UK2Node_Add_FloatFloat()
{
    TitleColor = ImColor(147, 226, 74);
}

void UK2Node_Add_FloatFloat::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Result");
}

void UK2Node_Add_FloatFloat::RenderBody()
{
}

FBlueprintValue UK2Node_Add_FloatFloat::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<float>(this, std::plus<float>(), Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Add_FloatFloat::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Subtract_FloatFloat)

UK2Node_Subtract_FloatFloat::UK2Node_Subtract_FloatFloat()
{
    TitleColor = ImColor(147, 226, 74);
}

void UK2Node_Subtract_FloatFloat::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Result");
}

void UK2Node_Subtract_FloatFloat::RenderBody()
{
}

FBlueprintValue UK2Node_Subtract_FloatFloat::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<float>(this, std::minus<float>(), Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Subtract_FloatFloat::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Multiply_FloatFloat)

UK2Node_Multiply_FloatFloat::UK2Node_Multiply_FloatFloat()
{
    TitleColor = ImColor(147, 226, 74);
}

void UK2Node_Multiply_FloatFloat::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Result");
}

void UK2Node_Multiply_FloatFloat::RenderBody()
{
}

FBlueprintValue UK2Node_Multiply_FloatFloat::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<float>(this, std::multiplies<float>(), Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Multiply_FloatFloat::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Divide_FloatFloat)

UK2Node_Divide_FloatFloat::UK2Node_Divide_FloatFloat()
{
    TitleColor = ImColor(147, 226, 74);
}

void UK2Node_Divide_FloatFloat::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Result");
}

void UK2Node_Divide_FloatFloat::RenderBody()
{
}

FBlueprintValue UK2Node_Divide_FloatFloat::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        // @note Float는 0으로 나누면 inf가 되며 크래시는 나지 않으므로 기본 연산자 사용
        return EvaluateBinaryOp<float>(this, std::divides<float>(), Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Divide_FloatFloat::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Greater_FloatFloat)

UK2Node_Greater_FloatFloat::UK2Node_Greater_FloatFloat()
{
    TitleColor = ImColor(220, 48, 48); // Bool 핀 색상 (가정)
}

void UK2Node_Greater_FloatFloat::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Greater_FloatFloat::RenderBody()
{
}

FBlueprintValue UK2Node_Greater_FloatFloat::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<float>(this, std::greater<float>(), Context);
    }
    return FBlueprintValue{};
}

void UK2Node_Greater_FloatFloat::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Equal_FloatFloat)

UK2Node_Equal_FloatFloat::UK2Node_Equal_FloatFloat()
{
    TitleColor = ImColor(220, 48, 48); // Bool 핀 색상 (가정)
}

void UK2Node_Equal_FloatFloat::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Equal_FloatFloat::RenderBody()
{
}

FBlueprintValue UK2Node_Equal_FloatFloat::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<float>(this, std::equal_to<float>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Equal_FloatFloat::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//  [Int] 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Add_IntInt)

UK2Node_Add_IntInt::UK2Node_Add_IntInt()
{
    TitleColor = ImColor(68, 201, 156);
}

void UK2Node_Add_IntInt::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Int, "Result");
}

void UK2Node_Add_IntInt::RenderBody()
{
}

FBlueprintValue UK2Node_Add_IntInt::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<int32>(this, std::plus<int32>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Add_IntInt::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Subtract_IntInt)

UK2Node_Subtract_IntInt::UK2Node_Subtract_IntInt()
{
    TitleColor = ImColor(68, 201, 156);
}

void UK2Node_Subtract_IntInt::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Int, "Result");
}

void UK2Node_Subtract_IntInt::RenderBody()
{
}

FBlueprintValue UK2Node_Subtract_IntInt::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<int32>(this, std::minus<int32>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Subtract_IntInt::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Multiply_IntInt)

UK2Node_Multiply_IntInt::UK2Node_Multiply_IntInt()
{
    TitleColor = ImColor(68, 201, 156);
}

void UK2Node_Multiply_IntInt::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Int, "Result");
}

void UK2Node_Multiply_IntInt::RenderBody()
{
}

FBlueprintValue UK2Node_Multiply_IntInt::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<int32>(this, std::multiplies<int32>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Multiply_IntInt::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Divide_IntInt)

UK2Node_Divide_IntInt::UK2Node_Divide_IntInt()
{
    TitleColor = ImColor(68, 201, 156);
}

void UK2Node_Divide_IntInt::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Int, "Result");
}

void UK2Node_Divide_IntInt::RenderBody()
{
}

FBlueprintValue UK2Node_Divide_IntInt::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        // @note Divide-by-Zero가 발생하지 않도록 주의해야 한다. 별도의 처리를 해주지 않는다.
        return EvaluateBinaryOp<int32>(this, std::divides<int32>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Divide_IntInt::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Greater_IntInt)

UK2Node_Greater_IntInt::UK2Node_Greater_IntInt()
{
    TitleColor = ImColor(220, 48, 48); // Bool 핀 색상 (가정)
}

void UK2Node_Greater_IntInt::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Greater_IntInt::RenderBody()
{
}

FBlueprintValue UK2Node_Greater_IntInt::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<int32>(this, std::greater<int32>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Greater_IntInt::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Equal_IntInt)

UK2Node_Equal_IntInt::UK2Node_Equal_IntInt()
{
    TitleColor = ImColor(220, 48, 48); // Bool 핀 색상 (가정)
}

void UK2Node_Equal_IntInt::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Equal_IntInt::RenderBody()
{
}

FBlueprintValue UK2Node_Equal_IntInt::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<int32>(this, std::equal_to<int32>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Equal_IntInt::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//  [Bool] 노드 (논리 연산)
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_And_BoolBool)

UK2Node_And_BoolBool::UK2Node_And_BoolBool()
{
    TitleColor = ImColor(220, 48, 48);
}

void UK2Node_And_BoolBool::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_And_BoolBool::RenderBody()
{
}

FBlueprintValue UK2Node_And_BoolBool::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<bool>(this, std::logical_and<bool>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_And_BoolBool::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Or_BoolBool)

UK2Node_Or_BoolBool::UK2Node_Or_BoolBool()
{
    TitleColor = ImColor(220, 48, 48);
}

void UK2Node_Or_BoolBool::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Or_BoolBool::RenderBody()
{
}

FBlueprintValue UK2Node_Or_BoolBool::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<bool>(this, std::logical_or<bool>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Or_BoolBool::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Xor_BoolBool)

UK2Node_Xor_BoolBool::UK2Node_Xor_BoolBool()
{
    TitleColor = ImColor(220, 48, 48);
}

void UK2Node_Xor_BoolBool::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "A");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "B");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Xor_BoolBool::RenderBody()
{
}

FBlueprintValue UK2Node_Xor_BoolBool::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateBinaryOp<bool>(this, std::not_equal_to<bool>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Xor_BoolBool::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_Not_Bool)

UK2Node_Not_Bool::UK2Node_Not_Bool()
{
    TitleColor = ImColor(220, 48, 48);
}

void UK2Node_Not_Bool::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "A"); // 입력 핀 1개
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_Not_Bool::RenderBody()
{
}

FBlueprintValue UK2Node_Not_Bool::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        return EvaluateUnaryOp<bool>(this, std::logical_not<bool>(), Context);
    }
    return FBlueprintValue{}; 
}

void UK2Node_Not_Bool::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}