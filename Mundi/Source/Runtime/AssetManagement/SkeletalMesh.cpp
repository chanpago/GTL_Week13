#include "pch.h"
#include "SkeletalMesh.h"
#include "Source/Editor/FBX/FbxLoader.h"
#include "WindowsBinReader.h"
#include "JsonSerializer.h"
#include <filesystem>

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::USkeletalMesh()
{
}

USkeletalMesh::~USkeletalMesh()
{
    ReleaseResources();
}

bool USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    if (Data)
    {
        ReleaseResources();
    }

    // FBXLoader가 캐싱을 내부적으로 처리합니다
    Data = UFbxLoader::GetInstance().LoadFbxMeshAsset(InFilePath);

    if (!Data || Data->Vertices.empty() || Data->Indices.empty())
    {
        UE_LOG("ERROR: Failed to load FBX mesh from '%s'", InFilePath.c_str());
        return false;
    }

    // Load associated metadata
    FString metaPath = InFilePath + ".meta.json";
    if (std::filesystem::exists(metaPath))
    {
        JSON metaJson;
        if (FJsonSerializer::LoadJsonFromFile(metaJson, UTF8ToWide(metaPath)))
        {
            FString physicsAssetPath;
            if (FJsonSerializer::ReadString(metaJson, "DefaultPhysicsAsset", physicsAssetPath))
            {
                if (!physicsAssetPath.empty())
                {
                    this->PhysicsAsset = UResourceManager::GetInstance().Load<UPhysicsAsset>(physicsAssetPath);
                    if (this->PhysicsAsset)
                    {
                        UE_LOG("Automatically loaded PhysicsAsset '%s' for SkeletalMesh '%s'", physicsAssetPath.c_str(), InFilePath.c_str());
                    }
                    else
                    {
                        UE_LOG("Failed to auto-load PhysicsAsset '%s' for SkeletalMesh '%s'", physicsAssetPath.c_str(), InFilePath.c_str());
                    }
                }
            }
        }
    }

    // GPU 버퍼 생성
    CreateIndexBuffer(Data, InDevice);
    VertexCount = static_cast<uint32>(Data->Vertices.size());
    IndexCount = static_cast<uint32>(Data->Indices.size());
    CPUSkinnedVertexStride = sizeof(FVertexDynamic);
    GPUSkinnedVertexStride = sizeof(FSkinnedVertex);

    // Cloth 데이터 자동 로드 (.cloth.json 파일이 있으면)
    LoadClothData("");

    return true;
}

void USkeletalMesh::ReleaseResources()
{
    if (IndexBuffer)
    {
        IndexBuffer->Release();
        IndexBuffer = nullptr;
    }

    if (Data)
    {
        delete Data;
        Data = nullptr;
    }
}

void USkeletalMesh::CreateCPUSkinnedVertexBuffer(ID3D11Buffer** InVertexBuffer)
{
    if (!Data) { return; }
    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    HRESULT hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(Device, Data->Vertices, InVertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::CreateGPUSkinnedVertexBuffer(ID3D11Buffer** InVertexBuffer)
{
    if (!Data)
    {
        return;
    }
    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    HRESULT hr = D3D11RHI::CreateVertexBuffer<FSkinnedVertex>(Device, Data->Vertices, InVertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::UpdateVertexBuffer(const TArray<FNormalVertex>& SkinnedVertices, ID3D11Buffer* InVertexBuffer)
{
    if (!InVertexBuffer) { return; }

    GEngine.GetRHIDevice()->VertexBufferUpdate(InVertexBuffer, SkinnedVertices);
}

void USkeletalMesh::CreateStructuredBuffer(ID3D11Buffer** InStructuredBuffer, ID3D11ShaderResourceView** InShaderResourceView, UINT ElementCount)
{
    if (!InStructuredBuffer || !InShaderResourceView || !Data)
    {
        return;
    }

    D3D11RHI *RHI = GEngine.GetRHIDevice();
    HRESULT hr = RHI->CreateStructuredBuffer(sizeof(FMatrix), ElementCount, nullptr, InStructuredBuffer);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateStructuredBuffer] Structured buffer ceation fail");
        return;
    }

    hr = RHI->CreateStructuredBufferSRV(*InStructuredBuffer, InShaderResourceView);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateStructuredBuffer] Structured bufferSRV ceation fail");
        return;
    }    
}

void USkeletalMesh::BuildLocalAABBs()
{
    if (!Data || Data->Vertices.IsEmpty() || Data->Skeleton.Bones.IsEmpty())
    {
        return;
    }

    const uint32 BoneCount = GetBoneCount();
    const FAABB InValidAABB = FAABB(FVector(FLT_MAX), FVector(-FLT_MAX));
    // 이전 AABB 초기화
    BoneLocalAABBs.Empty();
    BoneLocalAABBs.SetNum(BoneCount, InValidAABB);
    for (const FSkinnedVertex& Vertex : Data->Vertices)
    {
        // 최대 4개의 본이 영향을 줌
        for (int32 i = 0; i < 4; i++)
        {
            const float Weight = Vertex.BoneWeights[i];
            // 가중치가 0보다 커야 정점에 영향을 줌
            // 영향을 받는 정점만 계산
            if (Weight > 0)
            {
                const uint32 BoneIndex = Vertex.BoneIndices[i];

                if (BoneIndex < BoneCount)
                {
                    // Min, Max가 같은 PointAABB
                    FAABB PointAABB(Vertex.Position, Vertex.Position);
                    // PointAABB와 기존 AABB를 Union 해나간다.
                    BoneLocalAABBs[BoneIndex] = FAABB::Union(BoneLocalAABBs[BoneIndex], PointAABB);
                }
            }
        }
    }
}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMeshData* InSkeletalMesh, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InSkeletalMesh, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

FString USkeletalMesh::GetClothDataPath() const
{
    if (!Data || Data->PathFileName.empty())
    {
        return "";
    }
    // .fbx → .cloth.json
    std::filesystem::path MeshPath(Data->PathFileName.c_str());
    MeshPath.replace_extension(".cloth.json");
    return MeshPath.string().c_str();
}

bool USkeletalMesh::SaveClothData(const FString& FilePath)
{
    if (!Data)
    {
        UE_LOG("[USkeletalMesh] SaveClothData failed: No mesh data");
        return false;
    }

    JSON Root = JSON::Make(JSON::Class::Object);
    Root["Type"] = "ClothAssetData";
    Root["MeshPath"] = Data->PathFileName.c_str();

    // ClothAssets 배열 저장
    JSON ClothArray = JSON::Make(JSON::Class::Array);
    for (const FClothAssetData& Cloth : Data->ClothAssets)
    {
        JSON ClothJson = JSON::Make(JSON::Class::Object);

        ClothJson["SectionIndex"] = Cloth.SectionIndex;

        // ClothVertexIndices
        JSON VertexIndicesArray = JSON::Make(JSON::Class::Array);
        for (uint32 Idx : Cloth.ClothVertexIndices)
        {
            VertexIndicesArray.append(static_cast<int64>(Idx));
        }
        ClothJson["ClothVertexIndices"] = VertexIndicesArray;

        // FixedVertexIndices
        JSON FixedIndicesArray = JSON::Make(JSON::Class::Array);
        for (uint32 Idx : Cloth.FixedVertexIndices)
        {
            FixedIndicesArray.append(static_cast<int64>(Idx));
        }
        ClothJson["FixedVertexIndices"] = FixedIndicesArray;

        // Physics 설정
        ClothJson["Gravity"] = FJsonSerializer::VectorToJson(Cloth.Gravity);
        ClothJson["Damping"] = FJsonSerializer::VectorToJson(Cloth.Damping);
        ClothJson["SolverFrequency"] = Cloth.SolverFrequency;

        // Wind 설정
        ClothJson["WindVelocity"] = FJsonSerializer::VectorToJson(Cloth.WindVelocity);
        ClothJson["DragCoefficient"] = Cloth.DragCoefficient;
        ClothJson["LiftCoefficient"] = Cloth.LiftCoefficient;

        // Stiffness
        ClothJson["StretchStiffness"] = Cloth.StretchStiffness;
        ClothJson["BendStiffness"] = Cloth.BendStiffness;

        ClothArray.append(ClothJson);
    }
    Root["ClothAssets"] = ClothArray;

    // 파일 저장
    FString SavePath = FilePath.empty() ? GetClothDataPath() : FilePath;
    if (SavePath.empty())
    {
        UE_LOG("[USkeletalMesh] SaveClothData failed: Invalid path");
        return false;
    }

    if (FJsonSerializer::SaveJsonToFile(Root, FWideString(SavePath.begin(), SavePath.end())))
    {
        UE_LOG("[USkeletalMesh] ClothData saved: %s", SavePath.c_str());
        return true;
    }

    UE_LOG("[USkeletalMesh] SaveClothData failed: Could not write file");
    return false;
}

bool USkeletalMesh::LoadClothData(const FString& FilePath)
{
    if (!Data)
    {
        UE_LOG("[USkeletalMesh] LoadClothData failed: No mesh data");
        return false;
    }

    FString LoadPath = FilePath.empty() ? GetClothDataPath() : FilePath;
    if (LoadPath.empty())
    {
        return false;
    }

    // 파일 존재 확인
    if (!std::filesystem::exists(LoadPath.c_str()))
    {
        // Cloth 파일 없음 - 에러가 아님
        return false;
    }

    JSON Root;
    if (!FJsonSerializer::LoadJsonFromFile(Root, FWideString(LoadPath.begin(), LoadPath.end())))
    {
        UE_LOG("[USkeletalMesh] LoadClothData failed: Could not read file %s", LoadPath.c_str());
        return false;
    }

    // Type 확인
    FString Type;
    if (!FJsonSerializer::ReadString(Root, "Type", Type) || Type != "ClothAssetData")
    {
        UE_LOG("[USkeletalMesh] LoadClothData failed: Invalid file type");
        return false;
    }

    // ClothAssets 로드
    Data->ClothAssets.Empty();

    JSON ClothArray;
    if (FJsonSerializer::ReadArray(Root, "ClothAssets", ClothArray, JSON(), false))
    {
        for (int32 i = 0; i < ClothArray.size(); ++i)
        {
            JSON& ClothJson = ClothArray.at(i);
            FClothAssetData Cloth;

            FJsonSerializer::ReadInt32(ClothJson, "SectionIndex", Cloth.SectionIndex, -1, false);

            // ClothVertexIndices
            JSON VertexIndicesArray;
            if (FJsonSerializer::ReadArray(ClothJson, "ClothVertexIndices", VertexIndicesArray, JSON(), false))
            {
                for (int32 j = 0; j < VertexIndicesArray.size(); ++j)
                {
                    Cloth.ClothVertexIndices.Add(static_cast<uint32>(VertexIndicesArray.at(j).ToInt()));
                }
            }

            // FixedVertexIndices
            JSON FixedIndicesArray;
            if (FJsonSerializer::ReadArray(ClothJson, "FixedVertexIndices", FixedIndicesArray, JSON(), false))
            {
                for (int32 j = 0; j < FixedIndicesArray.size(); ++j)
                {
                    Cloth.FixedVertexIndices.Add(static_cast<uint32>(FixedIndicesArray.at(j).ToInt()));
                }
            }

            // Physics 설정
            FJsonSerializer::ReadVector(ClothJson, "Gravity", Cloth.Gravity, FVector(0.f, 0.f, -980.f), false);
            FJsonSerializer::ReadVector(ClothJson, "Damping", Cloth.Damping, FVector(0.2f, 0.2f, 0.2f), false);
            FJsonSerializer::ReadFloat(ClothJson, "SolverFrequency", Cloth.SolverFrequency, 120.f, false);

            // Wind 설정
            FJsonSerializer::ReadVector(ClothJson, "WindVelocity", Cloth.WindVelocity, FVector::Zero(), false);
            FJsonSerializer::ReadFloat(ClothJson, "DragCoefficient", Cloth.DragCoefficient, 0.5f, false);
            FJsonSerializer::ReadFloat(ClothJson, "LiftCoefficient", Cloth.LiftCoefficient, 0.3f, false);

            // Stiffness
            FJsonSerializer::ReadFloat(ClothJson, "StretchStiffness", Cloth.StretchStiffness, 1.0f, false);
            FJsonSerializer::ReadFloat(ClothJson, "BendStiffness", Cloth.BendStiffness, 0.5f, false);

            Data->ClothAssets.Add(std::move(Cloth));
        }
    }

    UE_LOG("[USkeletalMesh] ClothData loaded: %s (%d cloth assets)", LoadPath.c_str(), Data->ClothAssets.Num());
    return true;
}
