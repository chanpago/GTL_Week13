#include "pch.h"
#include "EdGraph.h"
#include "EdGraphNode.h"

IMPLEMENT_CLASS(UEdGraph)

UEdGraph::~UEdGraph()
{
    for (UEdGraphNode* Node : Nodes)
    {
        DeleteObject(Node);
    }
    Nodes.Empty();
}

void UEdGraph::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // #1. 기존 데이터 초기화
        for (UEdGraphNode* Node : Nodes)
        {
            DeleteObject(Node);
        }
        Nodes.Empty();

        // #2. 메타데이터 복구
        FJsonSerializer::ReadInt32(InOutHandle, "UniqueIDCounter", UniqueIDCounter, 1);

        // #3. 노드 재생성 (NodeID, 핀 데이터 포함)
        if (InOutHandle.hasKey("Nodes"))
        {
            JSON NodesArray = InOutHandle["Nodes"];
            int NodeCount = NodesArray.size();

            for (int i = 0; i < NodeCount; ++i)
            {
                JSON NodeJson = NodesArray[i];
                
                FString ClassName;
                FJsonSerializer::ReadString(NodeJson, "ClassName", ClassName);

                UEdGraphNode* NewNode = Cast<UEdGraphNode>(NewObject(UClass::FindClass(ClassName)));
                
                if (NewNode)
                {
                    AddNode(NewNode);
                    NewNode->Serialize(true, NodeJson);
                }
            }
        }

        // #4. 링크(연결) 복구 
        if (InOutHandle.hasKey("Links"))
        {
            JSON LinksArray = InOutHandle["Links"];
            int LinkCount = LinksArray.size();

            for (int i = 0; i < LinkCount; ++i)
            {
                JSON LinkJson = LinksArray[i];
                
                uint32 FromNodeID = 0;
                uint32 ToNodeID = 0;
                FString FromPinName;
                FString ToPinName;

                FJsonSerializer::ReadUint32(LinkJson, "FromNodeID", FromNodeID);
                FJsonSerializer::ReadString(LinkJson, "FromPinName", FromPinName);
                FJsonSerializer::ReadUint32(LinkJson, "ToNodeID", ToNodeID);
                FJsonSerializer::ReadString(LinkJson, "ToPinName", ToPinName);

                UEdGraphNode* FromNode = FindNode(FromNodeID);
                UEdGraphNode* ToNode = FindNode(ToNodeID);

                if (FromNode && ToNode)
                {
                    UEdGraphPin* PinA = FromNode->FindPin(FromPinName, EEdGraphPinDirection::EGPD_Output);
                    UEdGraphPin* PinB = ToNode->FindPin(ToPinName, EEdGraphPinDirection::EGPD_Input);

                    if (PinA && PinB)
                    {
                        PinA->MakeLinkTo(PinB);
                    }
                }
            }
        }
    }
    else
    {
        // #1. 메타데이터 저장
        InOutHandle["UniqueIDCounter"] = UniqueIDCounter;

        // #2. 노드 저장
        JSON NodesArray = JSON::Make(JSON::Class::Array);
        for (UEdGraphNode* Node : Nodes)
        {
            JSON NodeJson = JSON::Make(JSON::Class::Object);
            
            NodeJson["ClassName"] = Node->GetClass()->Name;
            Node->Serialize(false, NodeJson);
            
            NodesArray.append(NodeJson);
        }
        InOutHandle["Nodes"] = NodesArray;

        // #3. 링크 저장 
        JSON LinksArray = JSON::Make(JSON::Class::Array);
        for (UEdGraphNode* Node : Nodes)
        {
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
                {
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (LinkedPin && LinkedPin->OwningNode)
                        {
                            JSON LinkJson = JSON::Make(JSON::Class::Object);
                            
                            LinkJson["FromNodeID"] = Node->NodeID;
                            LinkJson["FromPinName"] = Pin->PinName;
                            
                            LinkJson["ToNodeID"] = LinkedPin->OwningNode->NodeID;
                            LinkJson["ToPinName"] = LinkedPin->PinName;
                            
                            LinksArray.append(LinkJson);
                        }
                    }
                }
            }
        }
        InOutHandle["Links"] = LinksArray;
    } 
}

UEdGraphNode* UEdGraph::AddNode(UEdGraphNode* Node)
{
    Nodes.Add(Node);
    return Node;
}

void UEdGraph::RemoveNode(UEdGraphNode* Node)
{
    if (Nodes.Contains(Node))
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            Pin->BreakAllLinks(); 
        }

        Nodes.Remove(Node);
        DeleteObject(Node);
    }
}

void UEdGraph::RemoveNode(uint32 NodeID)
{
    for (UEdGraphNode* Node : Nodes)
    {
        if (Node->NodeID == NodeID)
        {
            for (UEdGraphPin* Pin : Node->Pins)
            {
                Pin->BreakAllLinks();
            }
            
            Nodes.Remove(Node);
            DeleteObject(Node);
            
            break;
        }
    }
}

UEdGraphPin* UEdGraph::FindPin(uint32 PinID) const
{
    for (UEdGraphNode* Node : Nodes)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->PinID == PinID)
            {
                return Pin;
            }
        }
    }
    return nullptr;
}

UEdGraphNode* UEdGraph::FindNode(uint32 InNodeID) const
{
    for (UEdGraphNode* Node : Nodes)
    {
        if (Node->NodeID == InNodeID)
        {
            return Node; 
        }
    }
    return nullptr;
}

int32 UEdGraph::GetNextUniqueID()
{
    return UniqueIDCounter++;
}
