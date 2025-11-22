#pragma once
#include "ResourceBase.h"
#include <filesystem>

struct FShaderMacro
{
	FName Name;
	FName Definition;

	// TMap의 키로 사용하기 위해 비교 연산자 정의
	bool operator==(const FShaderMacro& Other) const
	{
		return Name == Other.Name && Definition == Other.Definition;
	}
};

// 단일 셰이더 파일의 여러 변형 중 하나
struct FShaderVariant
{
	ID3DBlob* VSBlob = nullptr;
	ID3DBlob* PSBlob = nullptr;
	ID3D11InputLayout* InputLayout = nullptr; // InputLayout은 VS Blob에 종속적이므로 함께 관리해야 합니다.
	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;

	// Store macros for hot reload
	TArray<FShaderMacro> SourceMacros;

	// 이 Variant에 속한 모든 리소스를 해제하는 헬퍼 함수
	void Release()
	{
		if (VSBlob) { VSBlob->Release(); VSBlob = nullptr; }
		if (PSBlob) { PSBlob->Release(); PSBlob = nullptr; }
		if (InputLayout) { InputLayout->Release(); InputLayout = nullptr; }
		if (VertexShader) { VertexShader->Release(); VertexShader = nullptr; }
		if (PixelShader) { PixelShader->Release(); PixelShader = nullptr; }
	}
};

class UShader : public UResourceBase
{
public:
	DECLARE_CLASS(UShader, UResourceBase)

	static uint64 GenerateShaderKey(const TArray<FShaderMacro>& InMacros);
	static FString GenerateMacrosToString(const TArray<FShaderMacro>& InMacros);	// UI 출력 or 디버깅용

	void Load(const FString& ShaderPath, ID3D11Device* InDevice, const TArray<FShaderMacro>& InMacros = TArray<FShaderMacro>());

	FShaderVariant* GetOrCompileShaderVariant(const TArray<FShaderMacro>& InMacros = TArray<FShaderMacro>());
	bool CompileVariantInternal(ID3D11Device* InDevice, const FString& InShaderPath, const TArray<FShaderMacro>& InMacros, FShaderVariant& OutVariant);
	//FShaderVariant* GetShaderVariant(const TArray<FShaderMacro>& InMacros = TArray<FShaderMacro>());
	ID3D11InputLayout* GetInputLayout(const TArray<FShaderMacro>& InMacros = TArray<FShaderMacro>());
	ID3D11VertexShader* GetVertexShader(const TArray<FShaderMacro>& InMacros = TArray<FShaderMacro>());
	ID3D11PixelShader* GetPixelShader(const TArray<FShaderMacro>& InMacros = TArray<FShaderMacro>());

	// Hot Reload Support
	bool IsOutdated() const;
	bool Reload(ID3D11Device* InDevice);
	//const TArray<FShaderMacro>& GetMacros() const { return Macros; }

	static bool HasMacro(const TArray<FShaderMacro>& InMacros, const FString& InMacroName);
	
protected:
	virtual ~UShader();

private:
	TMap<uint64, FShaderVariant> ShaderVariantMap;

	// Store included files (e.g., "Shaders/Common/LightingCommon.hlsl")
	// Used for hot reload - if any included file changes, reload this shader
	TArray<FString> IncludedFiles;
	TMap<FString, std::filesystem::file_time_type> IncludedFileTimestamps;

	void CreateInputLayout(ID3D11Device* Device, const FString& InShaderPath, FShaderVariant& InOutVariant);
	void ReleaseResources();

	// Include 파일 파싱 및 추적
	void ParseIncludeFiles(const FString& ShaderPath);
	void UpdateIncludeTimestamps();
};

struct FVertexPositionColor
{
	static const D3D11_INPUT_ELEMENT_DESC* GetLayout()
	{
		static const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		return layout;
	}
	static uint32 GetLayoutCount() { return 2; }
};

struct FVertexPositionColorTexturNormal
{
	static const D3D11_INPUT_ELEMENT_DESC* GetLayout()
	{
		static const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		return layout;
	}

	static uint32 GetLayoutCount() { return 4; }
};

struct FVertexPositionBillBoard
{
	static const D3D11_INPUT_ELEMENT_DESC* GetLayout()
	{
		static const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "WORLDPOSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{ "UVRECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		return layout;
	}

	static uint32 GetLayoutCount() { return 3; }
};

struct FVertexParticleSprite
{
	static const D3D11_INPUT_ELEMENT_DESC* GetLayout()
	{
		static const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // Corner
			{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,      0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // Size
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		return layout;
	}

	static uint32 GetLayoutCount() { return 4; }
};

// ======================== 오클루전 관련 메소드들 ============================
/*
// 실패 시 false 반환, *OutVS/*OutPS는 성공 시 유효
bool CompileVS(ID3D11Device* Dev, const wchar_t* FilePath, const char* Entry,
	ID3D11VertexShader** OutVS, ID3DBlob** OutVSBytecode = nullptr);

bool CompilePS(ID3D11Device* Dev, const wchar_t* FilePath, const char* Entry,
	ID3D11PixelShader** OutPS);
*/