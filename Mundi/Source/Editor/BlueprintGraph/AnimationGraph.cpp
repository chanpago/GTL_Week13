#include "pch.h"
#include "AnimationGraph.h"

#include "BlueprintActionDatabase.h"
#include "K2Node_Animation.h"

IMPLEMENT_CLASS(UAnimationGraph)

UAnimationGraph::UAnimationGraph()
{
    ValidateGraph(); 
}

UK2Node_AnimStateEntry* UAnimationGraph::GetEntryNode()
{
    for (UEdGraphNode* Node : Nodes)
    {
        if (auto Entry = Cast<UK2Node_AnimStateEntry>(Node))
        {
            return Entry; 
        }
    }
    return nullptr;
}

void UAnimationGraph::ValidateGraph()
{
    UEdGraphNode* EntryNode = GetEntryNode();

    if (EntryNode)
    {
        UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(UK2Node_AnimStateEntry::StaticClass());
        
        EntryNode = Spawner->SpawnNode(this, ImVec2(0, 0));
        
        AddNode(EntryNode);

        DeleteObject(Spawner);
    }
}
