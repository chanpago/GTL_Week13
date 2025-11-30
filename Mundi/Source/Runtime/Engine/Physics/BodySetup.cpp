#include "pch.h"
#include "BodySetup.h"
#include "PhysicalMaterial.h"

IMPLEMENT_CLASS(UBodySetup)


UBodySetup::UBodySetup()
{
	PhysMaterial = NewObject<UPhysicalMaterial>();
}

UBodySetup::~UBodySetup()
{
	if (PhysMaterial)
	{
		ObjectFactory::DeleteObject(PhysMaterial);
		PhysMaterial = nullptr;
	}
}

void UBodySetup::AddSphere(const FKSphereElem& Elem)
{
    AggGeom.SphereElements.Add(Elem);
    bCachedDataDirty = true;
}

void UBodySetup::AddBox(const FKBoxElem& Elem)
{
    AggGeom.BoxElements.Add(Elem);
    bCachedDataDirty = true;
}

void UBodySetup::AddSphyl(const FKSphylElem& Elem)
{
    AggGeom.SphylElements.Add(Elem);
    bCachedDataDirty = true;
}

void UBodySetup::BuildCachedData()
{
    // AggGeom을 돌면서 LocalBounds, Volume, Inertia 등 계산
    // 예: CachedBounds, CachedMass 같은 멤버 만들어서 세팅
}

void UBodySetup::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    // 부모 클래스의 직렬화 호출
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FString Bone;
        if (FJsonSerializer::ReadString(InOutHandle, "Bone", Bone))
        {
            BoneName = FName(Bone);
        }

        FJsonSerializer::ReadFloat(InOutHandle, "Mass", Mass, Mass, false);
        FJsonSerializer::ReadFloat(InOutHandle, "LinearDamping", LinearDamping, LinearDamping, false);
        FJsonSerializer::ReadFloat(InOutHandle, "AngularDamping", AngularDamping, AngularDamping, false);
        FJsonSerializer::ReadBool(InOutHandle,  "bSimulatePhysics", bSimulatePhysics, bSimulatePhysics, false);
        FJsonSerializer::ReadBool(InOutHandle,  "bEnableGravity",  bEnableGravity,  bEnableGravity,  false);

        AggGeom.Clear();
        if (InOutHandle.hasKey("Aggregate"))
        {
            AggGeom.Serialize(true, InOutHandle["Aggregate"]);
        }

        // PhysMaterial (있을 수도, 없을 수도)
        if (InOutHandle.hasKey("PhysMaterial"))
        {
            JSON& MatJson = InOutHandle["PhysMaterial"];
            if (!PhysMaterial)
            {
                PhysMaterial = NewObject<UPhysicalMaterial>();
            }
            PhysMaterial->Serialize(true, MatJson);
        }

        bCachedDataDirty = true;
    }
    else
    {
        InOutHandle["Bone"] = BoneName.ToString().c_str();
        InOutHandle["Mass"] = Mass;
        InOutHandle["LinearDamping"] = LinearDamping;
        InOutHandle["AngularDamping"] = AngularDamping;
        InOutHandle["SimulatePhysics"] = bSimulatePhysics;
        InOutHandle["EnableGravity"] = bEnableGravity;

        JSON AggJson = JSON::Make(JSON::Class::Object);
        AggGeom.Serialize(false, AggJson);
        InOutHandle["Aggregate"] = AggJson;

        if (PhysMaterial)
        {
            JSON MatJson = JSON::Make(JSON::Class::Object);
            PhysMaterial->Serialize(false, MatJson);
            InOutHandle["PhysMaterial"] = MatJson;
        }
    }
}

void FKAggregateGeom::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    auto SerializeArray = [&](const char* Key, auto& Array, auto&& ToJson, auto&& FromJson)
        {
            if (bInIsLoading)
            {
                Array.Empty();
                if (!InOutHandle.hasKey(Key)) return;
                const JSON& JsonArray = InOutHandle[Key];
                for (size_t i = 0; i < JsonArray.size(); ++i)
                {
                    typename std::remove_reference_t<decltype(Array[0])> Elem;
                    FromJson(JsonArray.at(i), Elem);
                    Array.Add(Elem);
                }
            }
            else
            {
                JSON JsonArray = JSON::Make(JSON::Class::Array);
                for (const auto& Elem : Array)
                {
                    JSON Entry = JSON::Make(JSON::Class::Object);
                    ToJson(Elem, Entry);
                    JsonArray.append(Entry);
                }
                InOutHandle[Key] = JsonArray;
            }
        };

    SerializeArray("Spheres", SphereElements,
        [](const FKSphereElem& Elem, JSON& Out)
        {
            Out["Center"] = FJsonSerializer::VectorToJson(Elem.Center);
            Out["Radius"] = Elem.Radius;
        },
        [](const JSON& In, FKSphereElem& Elem)
        {
            FJsonSerializer::ReadVector(In, "Center", Elem.Center);
            FJsonSerializer::ReadFloat(In, "Radius", Elem.Radius);
        });

    SerializeArray("Boxes", BoxElements,
        [](const FKBoxElem& Elem, JSON& Out)
        {
            Out["Center"] = FJsonSerializer::VectorToJson(Elem.Center);
            Out["Extents"] = FJsonSerializer::VectorToJson(Elem.Extents);
            Out["Rotation"] = FJsonSerializer::Vector4ToJson(FVector4(Elem.Rotation.X, Elem.Rotation.Y, Elem.Rotation.Z, Elem.Rotation.W));
        },
        [](const JSON& In, FKBoxElem& Elem)
        {
            FJsonSerializer::ReadVector(In, "Center", Elem.Center);
            FJsonSerializer::ReadVector(In, "Extents", Elem.Extents);
            FVector4 RotVec;
            FJsonSerializer::ReadVector4(In, "Rotation", RotVec);
            Elem.Rotation = FQuat(RotVec.X, RotVec.Y, RotVec.Z, RotVec.W);
        });

    SerializeArray("Sphyls", SphylElements,
        [](const FKSphylElem& Elem, JSON& Out)
        {
            Out["Center"] = FJsonSerializer::VectorToJson(Elem.Center);
            Out["Radius"] = Elem.Radius;
            Out["HalfLength"] = Elem.HalfLength;
            Out["Rotation"] = FJsonSerializer::Vector4ToJson(FVector4(Elem.Rotation.X, Elem.Rotation.Y, Elem.Rotation.Z, Elem.Rotation.W));
        },
        [](const JSON& In, FKSphylElem& Elem)
        {
            FJsonSerializer::ReadVector(In, "Center", Elem.Center);
            FJsonSerializer::ReadFloat(In, "Radius", Elem.Radius);
            FJsonSerializer::ReadFloat(In, "HalfLength", Elem.HalfLength);
            FVector4 RotVec;
            FJsonSerializer::ReadVector4(In, "Rotation", RotVec);
            Elem.Rotation = FQuat(RotVec.X, RotVec.Y, RotVec.Z, RotVec.W);
        });

    SerializeArray("Convexes", ConvexElements,
        [](const FKConvexElem& Elem, JSON& Out)
        {
            // Vertices 배열을 JSON 배열로 변환
            JSON VerticesArray = JSON::Make(JSON::Class::Array);
            for (const FVector& V : Elem.Vertices)
            {
                VerticesArray.append(FJsonSerializer::VectorToJson(V));
            }
            Out["Vertices"] = VerticesArray;
        },
        [](const JSON& In, FKConvexElem& Elem)
        {
            Elem.Vertices.Empty();
            if (!In.hasKey("Vertices"))
            {
                return;
            }

            const JSON& VerticesJsonArray = In.at("Vertices");
            for (size_t i = 0; i < VerticesJsonArray.size(); ++i)
            {
                const JSON& PointJson = VerticesJsonArray.at(i);
                if (PointJson.JSONType() != JSON::Class::Array || PointJson.size() != 3)
                {
                    continue;
                }

                FVector V;
                V.X = static_cast<float>(PointJson.at(0).ToFloat());
                V.Y = static_cast<float>(PointJson.at(1).ToFloat());
                V.Z = static_cast<float>(PointJson.at(2).ToFloat());
                Elem.Vertices.Add(V);
            }
        });
}