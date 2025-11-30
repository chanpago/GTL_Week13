#include "pch.h"
#include "TextRenderComponent.h"
#include "Shader.h"
#include "StaticMesh.h"
#include "Quad.h"
#include "StaticMeshComponent.h"
#include "BillboardComponent.h"
#include "FViewportClient.h"
#include "FViewport.h"
#include "World.h"
#include "RenderManager.h"
#include "WorldPartitionManager.h"
#include "Renderer.h"
#include "CameraActor.h"
#include "CameraComponent.h"
#include "PrimitiveComponent.h"
#include "StaticMeshActor.h"
#include "Gizmo/GizmoActor.h"
#include "Grid/GridActor.h"
#include "Octree.h"
#include "BVHierarchy.h"
#include "Occlusion.h"
#include "Frustum.h"
#include "ResourceManager.h"
#include "RHIDevice.h"
#include "Material.h"
#include "Texture.h"
#include "RenderSettings.h"
#include "EditorEngine.h"
#include "DecalComponent.h"
#include "DecalStatManager.h"
#include "SceneRenderer.h"
#include "SceneView.h"

#include <Windows.h>
#include "DirectionalLightComponent.h"
URenderer::URenderer(D3D11RHI* InDevice) : RHIDevice(InDevice)
{
	InitializeLineBatch();
}

URenderer::~URenderer()
{
	if (LineBatchData)
	{
		delete LineBatchData;
	}
}

void URenderer::BeginFrame()
{
	RHIDevice->IASetPrimitiveTopology();

	RHIDevice->OMSetRenderTargets(ERTVMode::BackBufferWithDepth);

	// 프레임별 데칼 통계를 추적하기 위해 초기화
	FDecalStatManager::GetInstance().ResetFrameStats();
	GPU_PROFILING_START;

	RHIDevice->ClearAllBuffer();
}

void URenderer::EndFrame()
{
	RHIDevice->Present();
}

void URenderer::RenderSceneForView(UWorld* World, FSceneView* View, FViewport* Viewport)
{
	// 씬을 그리는 FSceneRenderer 를 생성합니다.
	FSceneRenderer SceneRenderer(World, View, this);

	// 실제로 렌더를 수행합니다.
	SceneRenderer.Render();
}

UPrimitiveComponent* URenderer::GetPrimitiveCollided(int MouseX, int MouseY) const
{
	//GPU와 동기화 문제 때문에 Map이 호출될때까지 기다려야해서 피킹 하는 프레임에 엄청난 프레임 드랍이 일어남.
   //******비동기 방식으로 무조건 바꿔야함****************
	uint32 PickedId = 0;

	ID3D11DeviceContext* DeviceContext = RHIDevice->GetDeviceContext();
	//스테이징 버퍼를 가져와야 하는데 이걸 Device 추상 클래스가 Getter로 가지고 있는게 좋은 설계가 아닌 것 같아서 일단 캐스팅함


	D3D11_BOX Box{};
	Box.left = MouseX;
	Box.right = MouseX + 1;
	Box.top = MouseY;
	Box.bottom = MouseY + 1;
	Box.front = 0;
	Box.back = 1;

	DeviceContext->CopySubresourceRegion(
		RHIDevice->GetIdStagingBuffer(),
		0,
		0, 0, 0,
		RHIDevice->GetIdBuffer(),
		0,
		&Box);
	D3D11_MAPPED_SUBRESOURCE MapResource{};
	if (SUCCEEDED(DeviceContext->Map(RHIDevice->GetIdStagingBuffer(), 0, D3D11_MAP_READ, 0, &MapResource)))
	{
		PickedId = *static_cast<uint32*>(MapResource.pData);
		DeviceContext->Unmap(RHIDevice->GetIdStagingBuffer(), 0);
	}

	if (PickedId == 0)
		return nullptr;
	return Cast<UPrimitiveComponent>(GUObjectArray[PickedId]);
}

void URenderer::InitializeLineBatch()
{
	// Create UDynamicMesh for efficient line batching
	DynamicLineMesh = UResourceManager::GetInstance().Load<ULineDynamicMesh>("Line");

	// Initialize with maximum capacity (MAX_LINES * 2 vertices, MAX_LINES * 2 indices)
	uint32 maxVertices = MAX_LINES * 2;
	uint32 maxIndices = MAX_LINES * 2;
	DynamicLineMesh->Load(maxVertices, maxIndices, RHIDevice->GetDevice());

	// Create FMeshData for accumulating line data
	LineBatchData = new FMeshData();

	// Load line shader
	LineShader = UResourceManager::GetInstance().Load<UShader>("Shaders/UI/ShaderLine.hlsl");
}

void URenderer::BeginLineBatch()
{
	if (!LineBatchData) return;

	bLineBatchActive = true;

	// Clear previous batch data
	LineBatchData->Vertices.clear();
	LineBatchData->Color.clear();
	LineBatchData->Indices.clear();
}

void URenderer::AddLine(const FVector& Start, const FVector& End, const FVector4& Color)
{
	if (!bLineBatchActive || !LineBatchData) return;

	uint32 startIndex = static_cast<uint32>(LineBatchData->Vertices.size());

	// Add vertices
	LineBatchData->Vertices.push_back(Start);
	LineBatchData->Vertices.push_back(End);

	// Add colors
	LineBatchData->Color.push_back(Color);
	LineBatchData->Color.push_back(Color);

	// Add indices for line (2 vertices per line)
	LineBatchData->Indices.push_back(startIndex);
	LineBatchData->Indices.push_back(startIndex + 1);
}
void URenderer::AddLines(const TArray<FVector>& Lines, const FVector4& Color)
{
	if (!bLineBatchActive || !LineBatchData) return;
	size_t VerticesCount = Lines.size();
	if (VerticesCount % 2 != 0)
	{
		return;
	}

	uint32 startIndex = static_cast<uint32>(LineBatchData->Vertices.size());
	// Reserve space for efficiency

	LineBatchData->Vertices.reserve(LineBatchData->Vertices.size() + VerticesCount);
	LineBatchData->Color.reserve(LineBatchData->Color.size() + VerticesCount);
	LineBatchData->Indices.reserve(LineBatchData->Indices.size() + VerticesCount);

	// Add all lines at once
	for (size_t i = 0; i < VerticesCount; ++i)
	{
		// Add vertices
		LineBatchData->Vertices.push_back(Lines[i]);

		// Add colors
		LineBatchData->Color.push_back(Color);

		// Add indices for line (2 vertices per line)
		LineBatchData->Indices.push_back(startIndex + i);
	}
}
void URenderer::AddLinesRange(const TArray<FVector>& Lines, int startIdx, int Count, const FVector4& Color)
{
	if (!bLineBatchActive || !LineBatchData) return;
	size_t VerticesCount = Lines.size();
	if (VerticesCount % 2 != 0 || Count % 2 != 0)
	{
		return;
	}

	uint32 AddVerticesCount = Count;
	uint32 LineBatchStartIndex = static_cast<uint32>(LineBatchData->Vertices.size());
	// Reserve space for efficiency

	LineBatchData->Vertices.reserve(LineBatchData->Vertices.size() + AddVerticesCount);
	LineBatchData->Color.reserve(LineBatchData->Color.size() + AddVerticesCount);
	LineBatchData->Indices.reserve(LineBatchData->Indices.size() + AddVerticesCount);

	// Add all lines at once
	for (size_t i = 0; i < AddVerticesCount; ++i)
	{
		// Add vertices
		LineBatchData->Vertices.push_back(Lines[i + startIdx]);

		// Add colors
		LineBatchData->Color.push_back(Color);

		// Add indices for line (2 vertices per line)
		LineBatchData->Indices.push_back(LineBatchStartIndex + i);
	}
}

void URenderer::AddLines(const TArray<FVector>& StartPoints, const TArray<FVector>& EndPoints, const TArray<FVector4>& Colors)
{
	if (!bLineBatchActive || !LineBatchData) return;

	// Validate input arrays have same size
	if (StartPoints.size() != EndPoints.size() || StartPoints.size() != Colors.size())
		return;

	uint32 startIndex = static_cast<uint32>(LineBatchData->Vertices.size());

	// Reserve space for efficiency
	size_t lineCount = StartPoints.size();
	LineBatchData->Vertices.reserve(LineBatchData->Vertices.size() + lineCount * 2);
	LineBatchData->Color.reserve(LineBatchData->Color.size() + lineCount * 2);
	LineBatchData->Indices.reserve(LineBatchData->Indices.size() + lineCount * 2);

	// Add all lines at once
	for (size_t i = 0; i < lineCount; ++i)
	{
		uint32 currentIndex = startIndex + static_cast<uint32>(i * 2);

		// Add vertices
		LineBatchData->Vertices.push_back(StartPoints[i]);
		LineBatchData->Vertices.push_back(EndPoints[i]);

		// Add colors
		LineBatchData->Color.push_back(Colors[i]);
		LineBatchData->Color.push_back(Colors[i]);

		// Add indices for line (2 vertices per line)
		LineBatchData->Indices.push_back(currentIndex);
		LineBatchData->Indices.push_back(currentIndex + 1);
	}
}

void URenderer::EndLineBatch(const FMatrix& ModelMatrix)
{
	if (!bLineBatchActive || !LineBatchData || !DynamicLineMesh || LineBatchData->Vertices.empty())
	{
		bLineBatchActive = false;
		return;
	}

	// Clamp to GPU buffer capacity to avoid full drop when overflowing
	const uint32 totalLines = static_cast<uint32>(LineBatchData->Indices.size() / 2);
	if (totalLines > MAX_LINES)
	{
		const uint32 clampedLines = MAX_LINES;
		const uint32 clampedVerts = clampedLines * 2;
		const uint32 clampedIndices = clampedLines * 2;
		LineBatchData->Vertices.resize(clampedVerts);
		LineBatchData->Color.resize(clampedVerts);
		LineBatchData->Indices.resize(clampedIndices);
	}

	// Efficiently update dynamic mesh data (no buffer recreation!)
	if (!DynamicLineMesh->UpdateData(LineBatchData, RHIDevice->GetDeviceContext()))
	{
		bLineBatchActive = false;
		return;
	}

	// Set up rendering state
	FMatrix ModelInvTranspose = ModelMatrix.InverseAffine().Transpose();
	RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(ModelMatrix, ModelInvTranspose));
	RHIDevice->PrepareShader(LineShader);

	// Render using dynamic mesh
	if (DynamicLineMesh->GetCurrentVertexCount() > 0 && DynamicLineMesh->GetCurrentIndexCount() > 0)
	{
		UINT stride = sizeof(FVertexSimple);
		UINT offset = 0;

		ID3D11Buffer* vertexBuffer = DynamicLineMesh->GetVertexBuffer();
		ID3D11Buffer* indexBuffer = DynamicLineMesh->GetIndexBuffer();

		RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		RHIDevice->GetDeviceContext()->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
		RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		// Overlay 스텐실(=1) 영역은 그리지 않도록 스텐실 테스트 설정
		RHIDevice->OMSetDepthStencilState_StencilRejectOverlay();
		RHIDevice->GetDeviceContext()->DrawIndexed(DynamicLineMesh->GetCurrentIndexCount(), 0, 0);
		// 상태 복구
		RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);
	}

	bLineBatchActive = false;
}

void URenderer::EndLineBatchAlwaysOnTop(const FMatrix& ModelMatrix)
{
    if (!bLineBatchActive || !LineBatchData || !DynamicLineMesh || LineBatchData->Vertices.empty())
    {
        bLineBatchActive = false;
        return;
    }

    const uint32 totalLines = static_cast<uint32>(LineBatchData->Indices.size() / 2);
    if (totalLines > MAX_LINES)
    {
        const uint32 clampedLines = MAX_LINES;
        const uint32 clampedVerts = clampedLines * 2;
        const uint32 clampedIndices = clampedLines * 2;
        LineBatchData->Vertices.resize(clampedVerts);
        LineBatchData->Color.resize(clampedVerts);
        LineBatchData->Indices.resize(clampedIndices);
    }

    if (!DynamicLineMesh->UpdateData(LineBatchData, RHIDevice->GetDeviceContext()))
    {
        bLineBatchActive = false;
        return;
    }

    FMatrix ModelInvTranspose = ModelMatrix.InverseAffine().Transpose();
    RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(ModelMatrix, ModelInvTranspose));
    RHIDevice->PrepareShader(LineShader);

    if (DynamicLineMesh->GetCurrentVertexCount() > 0 && DynamicLineMesh->GetCurrentIndexCount() > 0)
    {
        UINT stride = sizeof(FVertexSimple);
        UINT offset = 0;
        ID3D11Buffer* vertexBuffer = DynamicLineMesh->GetVertexBuffer();
        ID3D11Buffer* indexBuffer = DynamicLineMesh->GetIndexBuffer();

        RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        RHIDevice->GetDeviceContext()->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        //RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Disable depth test so lines render on top
        RHIDevice->OMSetDepthStencilState(EComparisonFunc::Disable);
        RHIDevice->OMSetBlendState(true);
        RHIDevice->GetDeviceContext()->DrawIndexed(DynamicLineMesh->GetCurrentIndexCount(), 0, 0);
        // Restore state
        RHIDevice->OMSetBlendState(false);
        RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);
    }

    bLineBatchActive = false;
}

void URenderer::ClearLineBatch()
{
	if (!LineBatchData) return;

	LineBatchData->Vertices.clear();
	LineBatchData->Color.clear();
	LineBatchData->Indices.clear();

	bLineBatchActive = false;
}
