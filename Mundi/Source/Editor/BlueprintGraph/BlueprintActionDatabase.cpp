#include "pch.h"
#include "BlueprintActionDatabase.h"
#include "EdGraphNode.h"
#include "K2Node.h"
#include "ObjectIterator.h"

IMPLEMENT_CLASS(UBlueprintNodeSpawner)

UEdGraphNode* UBlueprintNodeSpawner::SpawnNode(UEdGraph* Graph, ImVec2 Position)
{
    assert(NodeClass->IsChildOf(UEdGraphNode::StaticClass()));
    
    auto NewNode = Cast<UEdGraphNode>(NewObject(NodeClass));
    if (NewNode)
    {
        NewNode->Position = Position;
        NewNode->bIsPositionSet = false;
        NewNode->AllocateDefaultPins();
    }

    return NewNode;
}

UBlueprintNodeSpawner* UBlueprintNodeSpawner::Create(UClass* InNodeClass)
{
    if (!InNodeClass || !InNodeClass->IsChildOf(UEdGraphNode::StaticClass()))
    {
        return nullptr;
    }

    UBlueprintNodeSpawner* Spawner = NewObject<UBlueprintNodeSpawner>();
    Spawner->NodeClass = InNodeClass;

    return Spawner;
}

FBlueprintActionDatabase::~FBlueprintActionDatabase()
{
    Shutdown();
}

void FBlueprintActionDatabase::Initialize()
{
    FBlueprintActionDatabaseRegistrar Registrar;

    /** @todo TObjectIterator<UClass>가 불가능해서 이런 방법 채택함. 더 나은 방법이 있는지 확인 필요 */
    for (auto [Class, Func] : ObjectFactory::GetRegistry())
    {
        /** @todo 추상 클래스가 등록되었을 경우 문제가 발생할 수도 있음. */
        if (Class->IsChildOf(UK2Node::StaticClass()))
        {
            /** 임시객체를 생성한다. */
            UK2Node* Node = Cast<UK2Node>(ConstructObject(Class));
            if (Node)
            {
                Node->GetMenuActions(Registrar);
                
                /** 임시객체를 해제한다. */
                delete Node;
            }
        }
    }

    RegisterAllNodeActions(std::move(Registrar));
}

void FBlueprintActionDatabase::Shutdown()
{
    // 이미 청소했으면 패스
    if (ActionRegistry.Num() == 0) return;

    for (UBlueprintNodeSpawner* Action : ActionRegistry)
    {
        if (Action)
        {
            DeleteObject(Action);
        }
    }
    ActionRegistry.Empty();
}

void FBlueprintActionDatabase::RegisterAllNodeActions(FBlueprintActionDatabaseRegistrar&& Registrar)
{
    ActionRegistry = std::move(Registrar.Spawners); 
}
