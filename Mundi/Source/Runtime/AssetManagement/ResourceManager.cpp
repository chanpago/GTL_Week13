#include "pch.h"
#include "MeshLoader.h"
#include "ObjectFactory.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "ObjManager.h"
#include "Quad.h"
#include "MeshBVH.h"
#include "Enums.h"

#include <filesystem>
#include <cwctype>

IMPLEMENT_CLASS(UResourceManager)

#define GRIDNUM 100
#define AXISLENGTH 100

UResourceManager::~UResourceManager()
{
    Clear();
}

UResourceManager& UResourceManager::GetInstance()
{
    static UResourceManager* Instance = nullptr;
        if (Instance == nullptr)
    {
        Instance = NewObject<UResourceManager>();
    }
    return *Instance;
}

void UResourceManager::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
    Device = InDevice;
    Resources.SetNum(static_cast<uint8>(EResourceType::End));

    Context = InContext;
    //CreateGridMesh(GRIDNUM,"Grid");
    //CreateAxisMesh(AXISLENGTH,"Axis");

    InitShaderILMap();

    InitTexToShaderMap();

    CreateTextBillboardMesh();//"TextBillboard"
    CreateBillboardMesh(); // Billboard
    CreateTextBillboardTexture();
    CreateDefaultShader();
    CreateDefaultMaterial();
}

// 전체 해제
void UResourceManager::Clear()
{
    {////////////// Deprecated //////////////
        for (auto& [Key, Data] : ResourceMap)
        {
            if (Data)
            {
                if (Data->VertexBuffer)
                {
                    Data->VertexBuffer->Release();
                    Data->VertexBuffer = nullptr;
                }
                if (Data->IndexBuffer)
                {
                    Data->IndexBuffer->Release();
                    Data->IndexBuffer = nullptr;
                }
                delete Data;
            }
        }
        ResourceMap.clear();


        // TextureMap 해제
        for (auto& [Key, Data] : TextureMap)
        {
            if (Data)
            {
                if (Data->Texture) { Data->Texture->Release(); Data->Texture = nullptr; }
                if (Data->TextureSRV) { Data->TextureSRV->Release(); Data->TextureSRV = nullptr; }
                if (Data->BlendState) { Data->BlendState->Release(); Data->BlendState = nullptr; }
                delete Data;
            }
        }
        TextureMap.clear();

        // MaterialMap 해제
        for (auto& [Key, Mat] : MaterialMap)
        {
            if (Mat)
            {
                ObjectFactory::DeleteObject(Mat);
            }
        }
        MaterialMap.clear();

        // Mesh BVH cache clear
        for (auto& Pair : MeshBVHCache)
        {
            delete Pair.second;
        }
        MeshBVHCache.clear();
    }

    for (auto& Array : Resources)
    {
        for (auto& Resource : Array)
        {
            if(Resource.second)
            {
                DeleteObject(Resource.second);
                Resource.second = nullptr;
            }
        }
        Array.Empty();
    }
    Resources.Empty();

    // Instance lifetime is managed by ObjectFactory
}

FMeshBVH* UResourceManager::GetMeshBVH(const FString& ObjPath)
{
    if (auto* Found = MeshBVHCache.Find(ObjPath))
        return *Found;
    return nullptr;
}

FMeshBVH* UResourceManager::GetOrBuildMeshBVH(const FString& ObjPath, const FStaticMesh* StaticMeshAsset)
{
    if (auto* Found = MeshBVHCache.Find(ObjPath))
        return *Found;

    if (!StaticMeshAsset)
        return nullptr;

    FMeshBVH* NewBVH = new FMeshBVH();
    NewBVH->Build(StaticMeshAsset->Vertices, StaticMeshAsset->Indices);
    MeshBVHCache.Add(ObjPath, NewBVH);
    return NewBVH;
}

void UResourceManager::SetStaticMeshs()
{
    StaticMeshs = GetAll<UStaticMesh>();
}

void UResourceManager::SetSkeletalMeshs()
{
    SkeletalMeshs = GetAll<USkeletalMesh>();
}

void UResourceManager::SetAudioFiles()
{ 
    Sounds = GetAll<USound>();
}


void UResourceManager::CreateAxisMesh(float Length, const FString& FilePath)
{
    // 이미 있으면 패스
    if (ResourceMap[FilePath])
    {
        return;
    }

    TArray<FVector> axisVertices;
    TArray<FVector4> axisColors;
    TArray<uint32> axisIndices;

    // X축 (빨강)
    axisVertices.push_back(FVector(0.0f, 0.0f, 0.0f));       // 원점
    axisVertices.push_back(FVector(Length, 0.0f, 0.0f));     // +X
    axisColors.push_back(FVector4(1.0f, 0.0f, 0.0f, 1.0f));  // 빨강
    axisColors.push_back(FVector4(1.0f, 0.0f, 0.0f, 1.0f));  // 빨강
    axisIndices.push_back(0);
    axisIndices.push_back(1);

    // Y축 (초록)
    axisVertices.push_back(FVector(0.0f, 0.0f, 0.0f));       // 원점
    axisVertices.push_back(FVector(0.0f, Length, 0.0f));     // +Y
    axisColors.push_back(FVector4(0.0f, 1.0f, 0.0f, 1.0f));  // 초록
    axisColors.push_back(FVector4(0.0f, 1.0f, 0.0f, 1.0f));  // 초록
    axisIndices.push_back(2);
    axisIndices.push_back(3);

    // Z축 (파랑)
    axisVertices.push_back(FVector(0.0f, 0.0f, 0.0f));       // 원점
    axisVertices.push_back(FVector(0.0f, 0.0f, Length));     // +Z
    axisColors.push_back(FVector4(0.0f, 0.0f, 1.0f, 1.0f));  // 파랑
    axisColors.push_back(FVector4(0.0f, 0.0f, 1.0f, 1.0f));  // 파랑
    axisIndices.push_back(4);
    axisIndices.push_back(5);

    FMeshData* MeshData = new FMeshData();
    MeshData->Vertices = axisVertices;
    MeshData->Color = axisColors;
    MeshData->Indices = axisIndices;

    UStaticMesh* Mesh = NewObject<UStaticMesh>();
    Mesh->Load(MeshData, Device);
    Add<UStaticMesh>("Axis", Mesh);

    UMeshLoader::GetInstance().AddMeshData("Axis", MeshData);
}

void UResourceManager::CreateTextBillboardMesh()
{
    TArray<uint32> Indices;
    for (uint32 i = 0;i < 100;i++)
    {
        Indices.push_back(i * 4 + 0);
        Indices.push_back(i * 4 + 1);
        Indices.push_back(i * 4 + 2);

        Indices.push_back(i * 4 + 2);
        Indices.push_back(i * 4 + 1);
        Indices.push_back(i * 4 + 3);
    }


    //if(UResourceManager::GetInstance().GetInstance<UMaterial>())
    const uint32 MaxQuads = 100; // capacity
    FMeshData* BillboardData = new FMeshData;
    BillboardData->Indices = Indices;
    // Reserve capacity for MaxQuads (4 vertices per quad)
    BillboardData->Vertices.resize(MaxQuads * 4);
    BillboardData->Color.resize(MaxQuads * 4);
    BillboardData->UV.resize(MaxQuads * 4);

    UQuad* Mesh = NewObject<UQuad>();
    Mesh->Load(BillboardData, Device, true);
    Add<UQuad>("TextBillboard", Mesh);
    UMeshLoader::GetInstance().AddMeshData("TextBillboard", BillboardData);
}

// 단일 Quad
void UResourceManager::CreateBillboardMesh()
{
    // Quad 인덱스 (삼각형 2개)
    TArray<uint32> Indices;
    Indices.push_back(0); Indices.push_back(1); Indices.push_back(2);
    Indices.push_back(0); Indices.push_back(2); Indices.push_back(3);

    // 메시 데이터 준비
    FMeshData* BillboardData = new FMeshData;
    BillboardData->Indices = Indices;

    // 정점 4개 (Quad)
    BillboardData->Vertices.resize(4);
    BillboardData->Color.resize(4);
    BillboardData->UV.resize(4);

    // 로컬 좌표계 기준 Quad (-0.5~0.5)
    BillboardData->Vertices[0] = { -0.5f, -0.5f, 0.0f }; // left-bottom
    BillboardData->Vertices[1] = { -0.5f,  0.5f, 0.0f }; // left-top
    BillboardData->Vertices[2] = { 0.5f,  0.5f, 0.0f }; // right-top
    BillboardData->Vertices[3] = { 0.5f, -0.5f, 0.0f }; // right-bottom

    // UV (0~1)
    BillboardData->UV[0] = { 0.0f, 1.0f };
    BillboardData->UV[1] = { 0.0f, 0.0f };
    BillboardData->UV[2] = { 1.0f, 0.0f };
    BillboardData->UV[3] = { 1.0f, 1.0f };

    // 색상 (기본 흰색)
    for (int i = 0; i < 4; i++)
        BillboardData->Color[i] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // GPU 리소스 생성
    UQuad* Mesh = NewObject<UQuad>();
    Mesh->Load(BillboardData, Device);

    // 리소스 매니저에 등록
    Add<UQuad>("BillboardQuad", Mesh);

    // CPU 데이터 캐싱 (선택)
    UMeshLoader::GetInstance().AddMeshData("BillboardQuad", BillboardData);
}

void UResourceManager::CreateGridMesh(int N, const FString& FilePath)
{
    if (ResourceMap[FilePath])
    {
        return;
    }
    TArray<FVector> gridVertices;
    TArray<FVector4> gridColors;
    TArray<uint32> gridIndices;
    // Z축 방향 선
    for (int i = -N; i <= N; i++)
    {
        if (i == 0)
        {
            continue;
        }
        // 색 결정: 5의 배수면 흰색, 아니면 회색
        float r = 0.1f, g = 0.1f, b = 0.1f;
        if (i % 5 == 0) { r = g = b = 0.4f; }
        if (i % 10 == 0) { r = g = b = 1.0f; }

        // 정점 2개 추가 (Z축 방향 라인)
        gridVertices.push_back(FVector((float)i, 0.0f, (float)-N));
        gridVertices.push_back(FVector((float)i, 0.0f, (float)N));
        gridColors.push_back(FVector4(r, g, b, 1.0f));
        gridColors.push_back(FVector4(r, g, b, 1.0f));

        // 인덱스 추가
        uint32 base = static_cast<uint32>(gridVertices.size());
        gridIndices.push_back(base - 2);
        gridIndices.push_back(base - 1);
    }

    // X축 방향 선
    for (int j = -N; j <= N; j++)
    {
        if (j == 0)
        {
            continue;
        }
        // 색 결정: 5의 배수면 흰색, 아니면 회색
        float r = 0.1f, g = 0.1f, b = 0.1f;

        if (j % 5 == 0) { r = g = b = 0.4f; }
        if (j % 10 == 0) { r = g = b = 1.0f; }

        // 정점 2개 추가 (X축 방향 라인)
        gridVertices.push_back(FVector((float)-N, 0.0f, (float)j));
        gridVertices.push_back(FVector((float)N, 0.0f, (float)j));
        gridColors.push_back(FVector4(r, g, b, 1.0f));
        gridColors.push_back(FVector4(r, g, b, 1.0f));

        // 인덱스 추가
        uint32 base = static_cast<uint32>(gridVertices.size());
        gridIndices.push_back(base - 2);
        gridIndices.push_back(base - 1);
    }

    gridVertices.push_back(FVector(0.0f, 0.0f, (float)-N));
    gridVertices.push_back(FVector(0.0f, 0.0f, 0.0f));
    gridColors.push_back(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    gridColors.push_back(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    uint32 base = static_cast<uint32>(gridVertices.size());
    gridIndices.push_back(base - 2);
    gridIndices.push_back(base - 1);

    gridVertices.push_back(FVector((float)-N, 0.0f, 0.0f));
    gridVertices.push_back(FVector(0.0f, 0.0f, 0.0f));
    gridColors.push_back(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    gridColors.push_back(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    base = static_cast<uint32>(gridVertices.size());
    gridIndices.push_back(base - 2);
    gridIndices.push_back(base - 1);

    FMeshData* MeshData = new FMeshData();
    MeshData->Vertices = gridVertices;
    MeshData->Color = gridColors;
    MeshData->Indices = gridIndices;

    UStaticMesh* Mesh = NewObject<UStaticMesh>();
    Mesh->Load(MeshData, Device);
    Add<UStaticMesh>("Grid", Mesh);

    UMeshLoader::GetInstance().AddMeshData("Grid", MeshData);
}

void UResourceManager::CreateBoxWireframeMesh(const FVector& Min, const FVector& Max, const FString& FilePath)
{
    // 이미 있으면 패스
    if (ResourceMap[FilePath])
    {
        return;
    }

    TArray<FVector> vertices;
    TArray<FVector4> colors;
    TArray<uint32> indices;

    // ─────────────────────────────
    // 8개의 꼭짓점 (AABB)
    // ─────────────────────────────
    vertices.push_back(FVector(Min.X, Min.Y, Min.Z)); // 0
    vertices.push_back(FVector(Max.X, Min.Y, Min.Z)); // 1
    vertices.push_back(FVector(Max.X, Max.Y, Min.Z)); // 2
    vertices.push_back(FVector(Min.X, Max.Y, Min.Z)); // 3
    vertices.push_back(FVector(Min.X, Min.Y, Max.Z)); // 4
    vertices.push_back(FVector(Max.X, Min.Y, Max.Z)); // 5
    vertices.push_back(FVector(Max.X, Max.Y, Max.Z)); // 6
    vertices.push_back(FVector(Min.X, Max.Y, Max.Z)); // 7

    // 색상 (디버깅용 흰색)
    for (int i = 0; i < 8; i++)
    {
        colors.push_back(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // ─────────────────────────────
    // 12개의 선 (24 인덱스)
    // ─────────────────────────────
    uint32 boxIndices[] = {
        // 바닥
        0, 1, 1, 2, 2, 3, 3, 0,
        // 천장
        4, 5, 5, 6, 6, 7, 7, 4,
        // 기둥
        0, 4, 1, 5, 2, 6, 3, 7
    };

    indices.insert(indices.end(), std::begin(boxIndices), std::end(boxIndices));

    // ─────────────────────────────
    // MeshData 생성 및 등록
    // ─────────────────────────────
    FMeshData* MeshData = new FMeshData();
    MeshData->Vertices = vertices;
    MeshData->Color = colors;
    MeshData->Indices = indices;

    UStaticMesh* Mesh = NewObject<UStaticMesh>();
    Mesh->Load(MeshData, Device);
    //Mesh->SetTopology(EPrimitiveTopology::LineList); // ✅ 꼭 LineList로 설정

    Add<UStaticMesh>(FilePath, Mesh);

    UMeshLoader::GetInstance().AddMeshData(FilePath, MeshData);
}

void UResourceManager::CreateDefaultShader()
{
    // 템플릿 Load 멤버함수 호출해서 Resources[UShader의 typeIndex][shader 파일 이름]에 UShader 포인터 할당
    Load<UShader>("Shaders/Primitives/Primitive.hlsl");
    Load<UShader>("Shaders/UI/Gizmo.hlsl");
    Load<UShader>("Shaders/UI/TextBillboard.hlsl");
    Load<UShader>("Shaders/UI/Billboard.hlsl");
    Load<UShader>("Shaders/Materials/Fireball.hlsl");
}

void UResourceManager::CreateDefaultMaterial()
{
    // 1. NewObject<UMaterial>()로 인스턴스 생성
    DefaultMaterialInstance = NewObject<UMaterial>();
    // 2. 엔진이 사용할 고유 경로(이름) 설정
    FString ShaderPath = "Shaders/Materials/UberLit.hlsl";
    UShader* DefaultShader = UResourceManager::GetInstance().Load<UShader>(ShaderPath);
    FString NewName = ShaderPath;
    DefaultMaterialInstance->SetMaterialName(NewName);
    DefaultMaterialInstance->SetShader(DefaultShader);

    // (참고: UMaterial에 SetMaterialName 같은 함수가 없다면 MaterialInfo.MaterialName을 직접 설정)
    // DefaultMaterialInstance->GetMaterialInfo().MaterialName = DefaultMaterialName;
    // 3. 리소스 매니저에 "UMaterial" 타입으로 등록 (핵심)
    Add<UMaterial>(NewName, DefaultMaterialInstance);
}

void UResourceManager::InitShaderILMap()
{
    TArray<D3D11_INPUT_ELEMENT_DESC> layout;

    layout.Add({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    ShaderToInputLayoutMap["Shaders/UI/Gizmo.hlsl"] = layout;
	layout.clear();

    layout.Add({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    ShaderToInputLayoutMap["Shaders/UI/ShaderLine.hlsl"] = layout;
    ShaderToInputLayoutMap["Shaders/Primitives/Primitive.hlsl"] = layout;
    layout.clear();

    layout.Add({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 });
   
    ShaderToInputLayoutMap["Shaders/Effects/Decal.hlsl"] = layout;
	ShaderToInputLayoutMap["Shaders/Materials/UberLit.hlsl"] = layout;
	ShaderToInputLayoutMap["Shaders/Materials/Fireball.hlsl"] = layout; // Use same vertex format as UberLit
	ShaderToInputLayoutMap["Shaders/Shadow/PointLightShadow.hlsl"] = layout;  // Shadow map rendering uses same vertex format
	ShaderToInputLayoutMap["Shaders/Shadows/DepthOnly_VS.hlsl"] = layout;
    layout.clear();

    layout.Add({ "WORLDPOSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "UVRECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 });
    ShaderToInputLayoutMap["Shaders/UI/TextBillboard.hlsl"] = layout;
    layout.clear();

    // ────────────────────────────────
    // 일반 빌보드 (Position + UV)
    // ────────────────────────────────
    layout.Add({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    ShaderToInputLayoutMap["Shaders/UI/Billboard.hlsl"] = layout;
    layout.clear();
    

    // ────────────────────────────────
    // Quad 렌더링을 쓰는 Shader들 (Position + UV)
    // ────────────────────────────────
    layout.Add({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 48,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    ShaderToInputLayoutMap["Shaders/PostProcess/HeightFog_PS.hlsl"] = layout;
    ShaderToInputLayoutMap["Shaders/Utility/SceneDepth_PS.hlsl"] = layout;
    layout.clear();
    
    // ────────────────────────────────
    // Particle 렌더링용 Shader 
    // ────────────────────────────────
    layout.Add({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 12,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,      0, 20,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    layout.Add({ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
                 D3D11_INPUT_PER_VERTEX_DATA, 0 });
    ShaderToInputLayoutMap["Shaders/Effects/ParticleSprite.hlsl"] = layout;
    layout.clear();

    ShaderToInputLayoutMap["Shaders/Utility/FullScreenTriangle_VS.hlsl"] = {};  // FullScreenTriangle 는 InputLayout을 사용하지 않는다
}

TArray<D3D11_INPUT_ELEMENT_DESC>& UResourceManager::GetProperInputLayout(const FString& InShaderName)
{
    auto it = ShaderToInputLayoutMap.find(InShaderName);

    if (it == ShaderToInputLayoutMap.end())
    {
        throw std::runtime_error("Proper input layout not found for " + InShaderName);
    }
    
    return ShaderToInputLayoutMap[InShaderName];
}

FString& UResourceManager::GetProperShader(const FString& InTextureName)
{
    auto it = TextureToShaderMap.find(InTextureName);

    if (it == TextureToShaderMap.end())
    {
        throw std::runtime_error("Proper shader not found for " + InTextureName);
    }

    return TextureToShaderMap[InTextureName];
}

void UResourceManager::InitTexToShaderMap()
{
    TextureToShaderMap["TextBillboard.dds"] = "TextBillboard.hlsl";
}


void UResourceManager::CreateTextBillboardTexture()
{
    UTexture* TextBillboardTexture = NewObject<UTexture>();
    TextBillboardTexture->Load("TextBillboard.dds",Device);
    Add<UTexture>("TextBillboard.dds", TextBillboardTexture);
}

void UResourceManager::CheckAndReloadShaders(float DeltaTime)
{
    // Throttle check frequency to reduce file system overhead
    ShaderCheckTimer += DeltaTime;
    if (ShaderCheckTimer < ShaderCheckInterval)
    {
        return;
    }
    ShaderCheckTimer = 0.0f;

    // Get all shader resources
    uint8 ShaderTypeIndex = static_cast<uint8>(EResourceType::Shader);
    if (ShaderTypeIndex >= Resources.size())
    {
        return;
    }

    // Check each shader for modifications
    TArray<UShader*> ShadersToReload;
    for (auto& Pair : Resources[ShaderTypeIndex])
    {
        UShader* Shader = static_cast<UShader*>(Pair.second);
        if (Shader && Shader->IsOutdated())
        {
            ShadersToReload.push_back(Shader);
        }
    }

    // Early exit if no shaders need reloading
    if (ShadersToReload.empty())
    {
        return;
    }

    // Ensure device context flushes before reloading
    if (Context)
    {
        Context->Flush();
    }

    // Reload outdated shaders
    for (UShader* Shader : ShadersToReload)
    {
        // Store old state to check if reload was successful
        bool bHadVertexShader = (Shader->GetVertexShader() != nullptr);
        bool bHadPixelShader = (Shader->GetPixelShader() != nullptr);

        if (Shader->Reload(Device))
        {
            // Verify the reload actually created the expected shader stages
            bool bHasVertexShader = (Shader->GetVertexShader() != nullptr);
            bool bHasPixelShader = (Shader->GetPixelShader() != nullptr);
            
            if ((bHadVertexShader && !bHasVertexShader) || (bHadPixelShader && !bHasPixelShader))
            {
                UE_LOG("Shader Hot Reload Warning: Some stages failed for %s", Shader->GetFilePath().c_str());
            }
            else
            {
                UE_LOG("Shader Hot Reload Successful: %s", Shader->GetFilePath().c_str());
            }
        }
        else
        {
            UE_LOG("Shader Hot Reload Failed: %s (keeping old shader)", Shader->GetFilePath().c_str());
        }
    }
}

void UResourceManager::UpdateDynamicVertexBuffer(const FString& Name, TArray<FBillboardVertexInfo_GPU>& vertices)
{
    UQuad* Mesh = Get<UQuad>(Name);

    const uint32_t quadCount = static_cast<uint32_t>(vertices.size() / 4);
    Mesh->SetIndexCount(quadCount * 6);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    Context->Map(Mesh->GetVertexBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, vertices.data(), sizeof(FBillboardVertexInfo_GPU) * vertices.size()); //vertices.size()만큼의 Character info를 vertices에서 pData로 복사해가라
    Context->Unmap(Mesh->GetVertexBuffer(), 0);
}

UMaterial* UResourceManager::GetDefaultMaterial()
{
    return DefaultMaterialInstance;
}

// 여기서 텍스처 데이터 로드 및 
FTextureData* UResourceManager::CreateOrGetTextureData(const FWideString& FilePath)
{
    auto it = TextureMap.find(FilePath);
    if (it != TextureMap.end())
    {
        return it->second;
    }

    FTextureData* Data = new FTextureData();

    // 확장자 판별 (안전)
    std::filesystem::path realPath(FilePath);
    std::wstring ext = realPath.has_extension() ? realPath.extension().wstring() : L"";
for (auto& ch : ext) ch = static_cast<wchar_t>(::towlower(ch));

    HRESULT hr = E_FAIL;
    if (ext == L".dds")
    {
        hr = DirectX::CreateDDSTextureFromFile(Device, FilePath.c_str(), &Data->Texture, &Data->TextureSRV, 0, nullptr);
    }
    else
    {
        hr = DirectX::CreateWICTextureFromFile(Device, Context, FilePath.c_str(), &Data->Texture, &Data->TextureSRV);
    }

    if (FAILED(hr) || Data->TextureSRV == nullptr)
    {
        // Fallback: Data 디렉토리 아래에서 파일명 일치 검색
        std::filesystem::path fname = std::filesystem::path(FilePath).filename();
        std::filesystem::path dataRoot = std::filesystem::absolute(GDataDir);
        bool retried = false;
        try {
            for (auto& entry : std::filesystem::recursive_directory_iterator(dataRoot))
            {
                if (!entry.is_regular_file()) continue;
                if (entry.path().filename() == fname)
                {
                    retried = true;
                    if (ext == L".dds")
                        hr = DirectX::CreateDDSTextureFromFile(Device, entry.path().c_str(), &Data->Texture, &Data->TextureSRV, 0, nullptr);
                    else
                        hr = DirectX::CreateWICTextureFromFile(Device, Context, entry.path().c_str(), &Data->Texture, &Data->TextureSRV);
                    if (SUCCEEDED(hr) && Data->TextureSRV) break;
                }
            }
        } catch(...) {}

        if (!retried || FAILED(hr) || Data->TextureSRV == nullptr)
        {
            if (Data->Texture) { Data->Texture->Release(); Data->Texture = nullptr; }
            if (Data->BlendState) { Data->BlendState->Release(); Data->BlendState = nullptr; }
            delete Data;
            UE_LOG("CreateOrGetTextureData failed: %ls\r\n", FilePath.c_str());
            return nullptr; // 실패 시 맵에 넣지 않음
        }
    }

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(Device->CreateBlendState(&blendDesc, &Data->BlendState)))
    {
        Data->BlendState = nullptr;
    }

    TextureMap[FilePath] = Data;
    return Data;
}
