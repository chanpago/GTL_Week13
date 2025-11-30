#pragma once
class UPhysicalMaterial : public UObject
{
    DECLARE_CLASS(UPhysicalMaterial, UObject)
public:
    float StaticFriction;
    float DynamicFriction;
    float Restitution;
    float Density;

    UPhysicalMaterial()
        : StaticFriction(0.5f)
        , DynamicFriction(0.5f)
        , Restitution(0.5f)
        , Density(1.0f)
    {
	}
    
    void Serialize(const bool bInIsLoading, JSON& InOutHandle)
    {
        Super::Serialize(bInIsLoading, InOutHandle);

        if (bInIsLoading)
        {
            FJsonSerializer::ReadFloat(InOutHandle, "StaticFriction", StaticFriction);
            FJsonSerializer::ReadFloat(InOutHandle, "DynamicFriction", DynamicFriction);
            FJsonSerializer::ReadFloat(InOutHandle, "Restitution", Restitution);
            FJsonSerializer::ReadFloat(InOutHandle, "Density", Density);
        }
        else
        {
            InOutHandle["StaticFriction"] = StaticFriction;
            InOutHandle["DynamicFriction"] = DynamicFriction;
            InOutHandle["Restitution"] = Restitution;
            InOutHandle["Density"] = Density;
        }
    }
};