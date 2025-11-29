#pragma once
#include "RHIDevice.h"
#include "ResourceManager.h"
#include "VertexData.h"
#include "ConstantBufferType.h"


#define DECLARE_CONSTANT_BUFFER(TYPE)\
ID3D11Buffer* TYPE##Buffer{};
#define CREATE_CONSTANT_BUFFER(TYPE)\
CreateConstantBuffer(&TYPE##Buffer, sizeof(TYPE));
#define RELEASE_CONSTANT_BUFFER(TYPE)\
{TYPE##Buffer->Release(); TYPE##Buffer = nullptr;}


#define DECLARE_UPDATE_CONSTANT_BUFFER_FUNC(TYPE) \
	void UpdateConstantBuffer(const TYPE& Data)	\
	{\
		ConstantBufferUpdate(TYPE##Buffer, Data);\
	}
#define DECLARE_SET_CONSTANT_BUFFER_FUNC(TYPE) \
	void SetConstantBuffer(const TYPE& Data)\
	{\
		ConstantBufferSet(TYPE##Buffer, TYPE##Slot, TYPE##IsVS, TYPE##IsPS);	\
	}
#define DECLARE_SET_UPDATE_CONSTANT_BUFFER_FUNC(TYPE) \
	void SetAndUpdateConstantBuffer(const TYPE& Data)	\
	{\
		ConstantBufferSetUpdate(TYPE##Buffer, Data, TYPE##Slot, TYPE##IsVS, TYPE##IsPS);	\
	}


struct FLinearColor;

enum class EComparisonFunc
{
	Always,
	LessEqual,
	GreaterEqual,
	Disable,
	LessEqualReadOnly,
	// 필요시 추가 후 OMSetDepthStencilState 함수 수정
};

class D3D11RHI
{
public:
	D3D11RHI() {};
	~D3D11RHI()
	{
		Release();
	}


public:
	void Initialize(HWND hWindow);

	void Release();


public:
	// clear
	void ClearAllBuffer();
	void ClearDepthBuffer(float Depth, UINT Stencil);
	void CreateBlendState();

	template<typename TVertex>
	static HRESULT CreateVertexBufferImpl(ID3D11Device* device, const FMeshData& mesh, ID3D11Buffer** outBuffer, D3D11_USAGE usage, UINT cpuAccessFlags);

	template<typename TVertex>
	static HRESULT CreateVertexBuffer(ID3D11Device* device, const FMeshData& mesh, ID3D11Buffer** outBuffer);

	template<typename TVertex>
	static HRESULT CreateVertexBufferImpl(ID3D11Device* Device, const std::vector<FNormalVertex>& SrcVertices, ID3D11Buffer** OutBuffer, D3D11_USAGE Usage, UINT CpuAccessFlags);

	template<typename TVertex>
	static HRESULT CreateVertexBufferImpl(ID3D11Device* Device, const std::vector<FSkinnedVertex>& SrcVertices, ID3D11Buffer** OutBuffer, D3D11_USAGE Usage, UINT CpuAccessFlags);

	template<typename TVertex>
	static HRESULT CreateVertexBuffer(ID3D11Device* device, const std::vector<FNormalVertex>& srcVertices, ID3D11Buffer** outBuffer);
	
	template<typename TVertex>
	static HRESULT CreateVertexBuffer(ID3D11Device* device, const std::vector<FSkinnedVertex>& srcVertices, ID3D11Buffer** outBuffer);

	static HRESULT CreateIndexBuffer(ID3D11Device* device, const FMeshData* meshData, ID3D11Buffer** outBuffer);

	static HRESULT CreateIndexBuffer(ID3D11Device* device, const FStaticMesh* mesh, ID3D11Buffer** outBuffer);
	
	static HRESULT CreateIndexBuffer(ID3D11Device* Device, const FSkeletalMeshData* Mesh, ID3D11Buffer** OutBuffer);

	CONSTANT_BUFFER_LIST(DECLARE_UPDATE_CONSTANT_BUFFER_FUNC)
	CONSTANT_BUFFER_LIST(DECLARE_SET_CONSTANT_BUFFER_FUNC)
	CONSTANT_BUFFER_LIST(DECLARE_SET_UPDATE_CONSTANT_BUFFER_FUNC)
	
	template <typename TVertex>
	void VertexBufferUpdate(ID3D11Buffer* VertexBuffer, const std::vector<TVertex>& Data)
	{
		// 데이터가 없으면 맵/언맵을 시도하지 않습니다.
		if (Data.empty()) { return; }

		D3D11_MAPPED_SUBRESOURCE MSR;
		DeviceContext->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MSR);

		const size_t DataSizeInBytes = Data.size() * sizeof(TVertex);
		memcpy(MSR.pData, Data.data(), DataSizeInBytes);

		DeviceContext->Unmap(VertexBuffer, 0);
	}
	template <typename T>
	void ConstantBufferUpdate(ID3D11Buffer* ConstantBuffer, T& Data)
	{
		D3D11_MAPPED_SUBRESOURCE MSR;

		DeviceContext->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MSR);
		memcpy(MSR.pData, &Data, sizeof(T));
		DeviceContext->Unmap(ConstantBuffer, 0);
	}
	template <typename T>
	void ConstantBufferSetUpdate(ID3D11Buffer* ConstantBuffer, T& Data, const uint32 Slot, const bool bIsVS, const bool bIsPS)
	{
		ConstantBufferUpdate(ConstantBuffer, Data);
		ConstantBufferSet(ConstantBuffer, Slot, bIsVS, bIsPS);
		
	}
	void ConstantBufferSet(ID3D11Buffer* ConstantBuffer, uint32 Slot, bool bIsVS, bool bIsPS);
    void UpdateUVScrollConstantBuffers(const FVector2D& Speed, float TimeSec);
	
	void IASetPrimitiveTopology();
	void RSSetState(ERasterizerMode ViewMode);
	void RSSetViewport();

	void OMSetBlendState(bool bIsBlendMode);
	void PSSetDefaultSampler(UINT StartSlot);
	void PSSetClampSampler(UINT StartSlot);

	void DrawFullScreenQuad();
	void Present();

	// Overlay precedence helpers
	void OMSetDepthStencilState_OverlayWriteStencil();
	void OMSetDepthStencilState_StencilRejectOverlay();

	void OMSetDepthStencilState(EComparisonFunc Func);

	void CreateShader(ID3D11InputLayout** OutSimpleInputLayout, ID3D11VertexShader** OutSimpleVertexShader, ID3D11PixelShader** OutSimplePixelShader);

	void OnResize(UINT NewWidth, UINT NewHeight);

	void CreateBackBufferAndDepthStencil(UINT width, UINT height);

	void SetViewport(UINT width, UINT height);

	// Viewport query
	UINT GetViewportWidth() const { return (UINT)ViewportInfo.Width; }
	UINT GetViewportHeight() const { return (UINT)ViewportInfo.Height; }

	// SwapChain 크기 (DOF 등에서 사용)
	UINT GetSwapChainWidth() const;
	UINT GetSwapChainHeight() const;

	void PrepareShader(UShader* InShader);
	void PrepareShader(UShader* InVertexShader, UShader* InPixelShader);

	// Structured Buffer 관련 메서드 (타일 기반 라이트 컬링용)
	HRESULT CreateStructuredBuffer(UINT InElementSize, UINT InElementCount, const void* InInitData, ID3D11Buffer** OutBuffer);
	HRESULT CreateStructuredBufferSRV(ID3D11Buffer* InBuffer, ID3D11ShaderResourceView** OutSRV);
	void UpdateStructuredBuffer(ID3D11Buffer* InBuffer, const void* InData, UINT InDataSize);

	// NOTE: 추후 private 로 이동 필요?
	// 현재 SRV, RTV 를 다루는 함수
	ID3D11RenderTargetView* GetCurrentTargetRTV() const;
	ID3D11RenderTargetView* GetIdBufferRTV() const { return IdBufferRTV; }

	ID3D11DepthStencilView* GetSceneDSV() const;

	ID3D11ShaderResourceView* GetSRV(RHI_SRV_Index SRVIndex) const;
	ID3D11ShaderResourceView* GetCurrentSourceSRV() const;

	ID3D11Texture2D* GetIdBuffer() const { return IdBuffer; }
	ID3D11Texture2D* GetIdStagingBuffer() const { return IdStagingBuffer; }

	void OMSetCustomRenderTargets(UINT NumRTVs, ID3D11RenderTargetView** RTVs, ID3D11DepthStencilView* DSV);

	// [Enum 으로 SRV, RTV 를 다루는 함수]
	ID3D11SamplerState* GetSamplerState(RHI_Sampler_Index SamplerIndex) const;
	void OMSetRenderTargets(ERTVMode RTVMode);

public:
	// getter
	inline ID3D11Device* GetDevice()
	{
		return Device;
	}
	inline ID3D11DeviceContext* GetDeviceContext()
	{
		return DeviceContext;
	}
	inline IDXGISwapChain* GetSwapChain()
	{
		return SwapChain;
	}

    // RTV Getters
    ID3D11RenderTargetView* GetBackBufferRTV() const { return BackBufferRTV; }
    ID3D11RenderTargetView** GetDOFRTVs() { return DOFRTVs; }
    ID3D11RenderTargetView* GetDOFRTV(int index) { return DOFRTVs[index]; }

private:
	void CreateDeviceAndSwapChain(HWND hWindow); // 여기서 디바이스, 디바이스 컨택스트, 스왑체인, 뷰포트를 초기화한다
	void CreateFrameBuffer();
	void CreateIdBuffer();
	void CreateDOFResources();  // DOF 렌더 타겟 생성
	void CreateRasterizerState();
	void CreateConstantBuffer(ID3D11Buffer** ConstantBuffer, uint32 Size);
	void CreateDepthStencilState();
	void CreateSamplerState();

	// release
	void ReleaseSamplerState();
	void ReleaseBlendState();
	void ReleaseRasterizerState(); // rs
	void ReleaseFrameBuffer(); // fb, rtv
	void ReleaseIdBuffer();
	void ReleaseDOFResources();  // DOF 렌더 타겟 해제
	void ReleaseDeviceAndSwapChain();

	// FSwapGuard 클래스가 D3D11RHI의 private 멤버에 접근할 수 있도록 허용
	friend class FSwapGuard;
	// 씬 컬러 버퍼의 읽기/쓰기 역할을 교환합니다. FSwapGuard에서만 호출하기 때문에 private으로 설정
	void SwapRenderTargets();

private:
	//24
	D3D11_VIEWPORT ViewportInfo{};

	//8
	ID3D11Device* Device{};//
	ID3D11DeviceContext* DeviceContext{};//
	IDXGISwapChain* SwapChain{};//

	ID3D11RasterizerState* DefaultRasterizerState{};//
	ID3D11RasterizerState* WireFrameRasterizerState{};//
	ID3D11RasterizerState* DecalRasterizerState{};//
	ID3D11RasterizerState* ShadowRasterizerState{};//
	ID3D11RasterizerState* NoCullRasterizerState{};//

	ID3D11DepthStencilState* DepthStencilState{};
	ID3D11DepthStencilState* DepthStencilStateLessEqualWrite = nullptr;      // 기본
	ID3D11DepthStencilState* DepthStencilStateLessEqualReadOnly = nullptr;   // 읽기 전용
	ID3D11DepthStencilState* DepthStencilStateAlwaysNoWrite = nullptr;       // 기즈모/오버레이
	ID3D11DepthStencilState* DepthStencilStateDisable = nullptr;              // 깊이 테스트/쓰기 모두 끔
	ID3D11DepthStencilState* DepthStencilStateGreaterEqualWrite = nullptr;   // 선택사항
	// Stencil-based overlay control
	ID3D11DepthStencilState* DepthStencilStateOverlayWriteStencil = nullptr;   // overlay writes stencil=1
	ID3D11DepthStencilState* DepthStencilStateStencilRejectOverlay = nullptr;  // draw only where stencil==0

	ID3D11BlendState* BlendStateTransparent{};
	ID3D11BlendState* BlendStateOpaque{};

	ID3D11Texture2D* FrameBuffer{};
	ID3D11Texture2D* IdBuffer{};
	ID3D11Texture2D* IdStagingBuffer{};

	ID3D11RenderTargetView* IdBufferRTV{};
	ID3D11RenderTargetView* BackBufferRTV{};
	ID3D11DepthStencilView* DepthStencilView{};

	static const int NUM_SCENE_BUFFERS = 2;
	// 씬 컬러 렌더링을 위한 핑퐁 버퍼 리소스
	ID3D11Texture2D* SceneColorTextures[NUM_SCENE_BUFFERS];
	ID3D11RenderTargetView* SceneColorRTVs[NUM_SCENE_BUFFERS];
	ID3D11ShaderResourceView* SceneColorSRVs[NUM_SCENE_BUFFERS];

	// 현재 소스(읽기)와 타겟(쓰기)을 가리키는 인덱스
	int32 SourceIndex = 0;
	int32 TargetIndex = 1;

	ID3D11Texture2D* DepthBuffer = nullptr;
	ID3D11ShaderResourceView* DepthSRV = nullptr;

	// DOF (Depth of Field) 렌더 타겟 (1/4 해상도)
	static const int NUM_DOF_BUFFERS = 4;
	ID3D11Texture2D* DOFTextures[NUM_DOF_BUFFERS] = {nullptr};
	ID3D11RenderTargetView* DOFRTVs[NUM_DOF_BUFFERS] = {nullptr};
	ID3D11ShaderResourceView* DOFSRVs[NUM_DOF_BUFFERS] = {nullptr};
	// [0] = Far Field, [1] = Near Field
	// [2] = Temp Horizontal Blur, [3] = Temp Vertical Blur

    // 버퍼 핸들
	CONSTANT_BUFFER_LIST(DECLARE_CONSTANT_BUFFER)
	ID3D11Buffer* UVScrollCB{};

	ID3D11SamplerState* DefaultSamplerState = nullptr;
	ID3D11SamplerState* LinearClampSamplerState = nullptr;
	ID3D11SamplerState* PointClampSamplerState = nullptr;
	ID3D11SamplerState* ShadowSamplerState = nullptr;
	ID3D11SamplerState* VSMSamplerState = nullptr;

	UShader* PreShader = nullptr; // Shaders, Inputlayout

	bool bReleased = false; // Prevent double Release() calls
};


template<typename TVertex>
inline HRESULT D3D11RHI::CreateVertexBufferImpl(ID3D11Device* device, const FMeshData& mesh, ID3D11Buffer** outBuffer, D3D11_USAGE usage, UINT cpuAccessFlags)
{
	std::vector<TVertex> vertexArray;
	vertexArray.reserve(mesh.Vertices.size());

	for (size_t i = 0; i < mesh.Vertices.size(); ++i)
	{
		TVertex vtx{};
		vtx.FillFrom(mesh, i);
		vertexArray.push_back(vtx);
	}

	D3D11_BUFFER_DESC vbd = {};
	vbd.Usage = usage;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = cpuAccessFlags;
	vbd.ByteWidth = static_cast<UINT>(sizeof(TVertex) * vertexArray.size());

	D3D11_SUBRESOURCE_DATA vinitData = {};
	vinitData.pSysMem = vertexArray.data();

	return device->CreateBuffer(&vbd, &vinitData, outBuffer);
}

// PositionColor → Static
template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FVertexSimple>(ID3D11Device* device, const FMeshData& mesh, ID3D11Buffer** outBuffer)
{
	return CreateVertexBufferImpl<FVertexSimple>(device, mesh, outBuffer,
		D3D11_USAGE_DEFAULT, 0);
}

// PositionColorTextureNormal → Static
template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FVertexDynamic>(ID3D11Device* device, const FMeshData& mesh, ID3D11Buffer** outBuffer)
{
	return CreateVertexBufferImpl<FVertexDynamic>(device, mesh, outBuffer,
		D3D11_USAGE_DEFAULT, 0);
}

// Billboard → Dynamic
template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FBillboardVertexInfo_GPU>(ID3D11Device* device, const FMeshData& mesh, ID3D11Buffer** outBuffer)
{
	return CreateVertexBufferImpl<FBillboardVertexInfo_GPU>(device, mesh, outBuffer,
		D3D11_USAGE_DYNAMIC,
		D3D11_CPU_ACCESS_WRITE);
}


template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FBillboardVertex>(ID3D11Device* device, const FMeshData& mesh, ID3D11Buffer** outBuffer)
{
	return CreateVertexBufferImpl<FBillboardVertex>(device, mesh, outBuffer,
		D3D11_USAGE_DYNAMIC,
		D3D11_CPU_ACCESS_WRITE);
}

template<typename TVertex>
inline HRESULT D3D11RHI::CreateVertexBufferImpl(ID3D11Device* Device, const std::vector<FNormalVertex>& SrcVertices, ID3D11Buffer** OutBuffer, D3D11_USAGE Usage, UINT CpuAccessFlags)
{
	std::vector<TVertex> VertexArray;
	VertexArray.reserve(SrcVertices.size());

	for (size_t i = 0; i < SrcVertices.size(); ++i)
	{
		TVertex Vertex{};
		Vertex.FillFrom(SrcVertices[i]); // 각 TVertex에서 FillFrom 구현 필요
		VertexArray.push_back(Vertex);
	}

	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = Usage;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = CpuAccessFlags;
	BufferDesc.ByteWidth = static_cast<UINT>(sizeof(TVertex) * VertexArray.size());

	D3D11_SUBRESOURCE_DATA InitData = {};
	InitData.pSysMem = VertexArray.data();

	return Device->CreateBuffer(&BufferDesc, &InitData, OutBuffer);
}

template <typename TVertex>
HRESULT D3D11RHI::CreateVertexBufferImpl(ID3D11Device* Device, const std::vector<FSkinnedVertex>& SrcVertices,
	ID3D11Buffer** OutBuffer, D3D11_USAGE Usage, UINT CpuAccessFlags)
{
	std::vector<TVertex> VertexArray;
	VertexArray.reserve(SrcVertices.size());

	for (size_t i = 0; i < SrcVertices.size(); ++i)
	{
		TVertex Vertex{};
		Vertex.FillFrom(SrcVertices[i]); // 각 TVertex에서 FillFrom 구현 필요
		VertexArray.push_back(Vertex);
	}

	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = Usage;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = CpuAccessFlags;
	BufferDesc.ByteWidth = static_cast<UINT>(sizeof(TVertex) * VertexArray.size());

	D3D11_SUBRESOURCE_DATA InitData = {};
	InitData.pSysMem = VertexArray.data();

	return Device->CreateBuffer(&BufferDesc, &InitData, OutBuffer);
}

template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FVertexSimple>(ID3D11Device* device, const std::vector<FNormalVertex>& srcVertices, ID3D11Buffer** outBuffer)
{
	return CreateVertexBufferImpl<FVertexSimple>(device, srcVertices, outBuffer, D3D11_USAGE_DEFAULT, 0);
}

// PositionColorTextureNormal
template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FVertexDynamic>(ID3D11Device* device, const std::vector<FNormalVertex>& srcVertices, ID3D11Buffer** outBuffer)
{
	return CreateVertexBufferImpl<FVertexDynamic>(device, srcVertices, outBuffer, D3D11_USAGE_DEFAULT, 0);
}

// Billboard
template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FBillboardVertexInfo_GPU>(ID3D11Device* device, const std::vector<FNormalVertex>& srcVertices, ID3D11Buffer** outBuffer)
{
	return CreateVertexBufferImpl<FBillboardVertexInfo_GPU>(device, srcVertices, outBuffer, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
}

template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FVertexDynamic>(ID3D11Device* Device, const std::vector<FSkinnedVertex>& SrcVertices, ID3D11Buffer** OutBuffer)
{
	std::vector<FVertexDynamic> VertexArray;
	VertexArray.reserve(SrcVertices.size());

	for (size_t Idx = 0; Idx < SrcVertices.size(); ++Idx)
	{
		const FSkinnedVertex& In = SrcVertices[Idx];
		FVertexDynamic Vertex{};

		Vertex.Position = In.Position;
		Vertex.Normal = In.Normal;
		Vertex.UV = In.UV;
		Vertex.Tangent = In.Tangent;
		Vertex.Color = In.Color;
		VertexArray.push_back(Vertex);
	}

	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	BufferDesc.ByteWidth = static_cast<UINT>(sizeof(FVertexDynamic) * VertexArray.size());

	D3D11_SUBRESOURCE_DATA InitData = {};
	InitData.pSysMem = VertexArray.data();

	return Device->CreateBuffer(&BufferDesc, &InitData, OutBuffer);
}

template<>
inline HRESULT D3D11RHI::CreateVertexBuffer<FSkinnedVertex>(ID3D11Device* Device, const std::vector<FSkinnedVertex>& SrcVertices, ID3D11Buffer** OutBuffer)
{
	return CreateVertexBufferImpl<FSkinnedVertex>(Device, SrcVertices, OutBuffer, D3D11_USAGE_DEFAULT, 0);
}