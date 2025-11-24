#include "pch.h"
#include "EdGraphNode.h"

IMPLEMENT_CLASS(UEdGraphNode)

void UEdGraphNode::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // #1. 노드 기본 속성 로드
        FJsonSerializer::ReadUint32(InOutHandle, "NodeID", NodeID);
        FJsonSerializer::ReadFloat(InOutHandle, "NodePosX", NodePosX);
        FJsonSerializer::ReadFloat(InOutHandle, "NodePosY", NodePosY);
        
        Position = ImVec2(NodePosX, NodePosY);
        bIsPositionSet = false;

        // #2. 핀 데이터 복구
        if (InOutHandle.hasKey("Pins"))
        {
            JSON PinsArray = InOutHandle["Pins"];
            int PinCount = PinsArray.size();

            for (int i = 0; i < PinCount; ++i)
            {
                JSON PinJson = PinsArray[i];

                UEdGraphPin* NewPin = NewObject<UEdGraphPin>(); 
                
                NewPin->OwningNode = this; 
                NewPin->Serialize(true, PinJson);

                Pins.Add(NewPin);
            }
        }
    }
    else
    {
        InOutHandle["NodeID"] = NodeID;
        InOutHandle["NodePosX"] = NodePosX;
        InOutHandle["NodePosY"] = NodePosY;

        JSON PinsArray = JSON::Make(JSON::Class::Array);
        for (UEdGraphPin* Pin : Pins)
        {
            JSON PinJson = JSON::Make(JSON::Class::Object);
            Pin->Serialize(false, PinJson);
            PinsArray.append(PinJson);
        }
        InOutHandle["Pins"] = PinsArray;
    }
}
