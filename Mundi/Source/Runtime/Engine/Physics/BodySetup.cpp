#include "pch.h"
#include "BodySetup.h"

IMPLEMENT_CLASS(UBodySetup)

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

            const JSON& VerticesJsonArray = In["Vertices"];
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

void UBodySetup::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    // 부모 클래스의 직렬화 호출
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // 로딩 시: 기존 데이터를 지우고 "Aggregate" 키가 있으면 직렬화
        AggGeom.Clear();
        if (InOutHandle.hasKey("Aggregate"))
        {
            AggGeom.Serialize(true, InOutHandle["Aggregate"]);
        }
    }
    else
    {
        // 저장 시: 새로운 JSON 객체를 만들고 AggGeom을 직렬화한 후 "Aggregate" 키에 할당
        JSON Aggregate = JSON::Make(JSON::Class::Object);
        AggGeom.Serialize(false, Aggregate);
        InOutHandle["Aggregate"] = Aggregate;
    }
}