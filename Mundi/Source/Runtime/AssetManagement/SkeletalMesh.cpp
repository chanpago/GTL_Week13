#include "pch.h"
#include "SkeletalMesh.h"
#include "Source/Editor/FBX/FbxLoader.h"
#include "WindowsBinReader.h"
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
