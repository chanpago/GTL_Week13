#include "pch.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "K2Node_Literal.h"
#include "BlueprintActionDatabase.h"

namespace ed = ax::NodeEditor;

// ----------------------------------------------------------------
//	[Int] 리터럴 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Literal_Int)

void UK2Node_Literal_Int::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadInt32(InOutHandle, "Value", Value);
    }
    else
    {
        InOutHandle["Value"] = Value;
    }
}

void UK2Node_Literal_Int::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Int, "Value");
}

void UK2Node_Literal_Int::RenderBody()
{
    ImGui::PushItemWidth(100.0f);
    ImGui::DragInt("##value", &Value, 0.1f);
    ImGui::PopItemWidth(); 
}

FBlueprintValue UK2Node_Literal_Int::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Value")
    {
        return Value;
    }
    return FBlueprintValue{};
}

void UK2Node_Literal_Int::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Float] 리터럴 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Literal_Float)

void UK2Node_Literal_Float::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadFloat(InOutHandle, "Value", Value);
    }
    else
    {
        InOutHandle["Value"] = Value;
    }
}

void UK2Node_Literal_Float::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Value");
}

void UK2Node_Literal_Float::RenderBody()
{
    ImGui::PushItemWidth(100.0f);
    ImGui::DragFloat("##value", &Value, 0.01f);
    ImGui::PopItemWidth();

}

FBlueprintValue UK2Node_Literal_Float::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Value")
    {
        return Value;
    }
    return FBlueprintValue{};
}

void UK2Node_Literal_Float::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Bool] 리터럴 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Literal_Bool)

void UK2Node_Literal_Bool::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadBool(InOutHandle, "Value", Value);
    }
    else
    {
        InOutHandle["Value"] = Value;
    }
}

void UK2Node_Literal_Bool::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Value");
}

void UK2Node_Literal_Bool::RenderBody()
{
    ImGui::Checkbox("##value", &Value);
    ImGui::SameLine();
    ImGui::Text(Value ? "True" : "False");
}

FBlueprintValue UK2Node_Literal_Bool::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Value")
    {
        return Value;
    }
    return FBlueprintValue{};
}

void UK2Node_Literal_Bool::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}
