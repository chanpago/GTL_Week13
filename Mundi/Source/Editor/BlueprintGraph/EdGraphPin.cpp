#include "pch.h"
#include "EdGraphPin.h"

IMPLEMENT_CLASS(UEdGraphPin)

void UEdGraphPin::Serialize(const bool bInIsLoading, JSON& InOutHandle) 
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadString(InOutHandle, "PinName", PinName);
        FJsonSerializer::ReadString(InOutHandle, "DefaultValue", DefaultValue);
        
        int32 DirInt = 0;
        FJsonSerializer::ReadInt32(InOutHandle, "Direction", DirInt);
        Direction = static_cast<EEdGraphPinDirection>(DirInt);
        
        FString CategoryStr;
        FJsonSerializer::ReadString(InOutHandle, "PinCategory", CategoryStr);
        PinType.PinCategory = FName(CategoryStr);
    }
    else
    {
        // @note PinID는 UUID와 일치해야 한다. (생성자가 할당한 값을 사용함)
        //       핀 정보를 직렬화할때는 PinID에 의존하지 않도록 주의해야 한다.
        // InOutHandle["PinID"] = PinID;
        InOutHandle["PinName"] = PinName; 
        InOutHandle["DefaultValue"] = DefaultValue;
        InOutHandle["Direction"] = static_cast<int32>(Direction);
        InOutHandle["PinCategory"] = PinType.PinCategory.ToString().c_str();
    }
}
