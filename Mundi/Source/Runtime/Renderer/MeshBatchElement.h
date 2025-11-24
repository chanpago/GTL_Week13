#pragma once
#include "pch.h"

// 전방 선언
class UShader;
class UMaterial;

/**
 * @struct FMeshBatchElement
 * @brief 단일 드로우 콜(Draw Call)을 위한 모든 렌더링 정보를 집계하는 원자 단위 구조체입니다.
 */
struct FMeshBatchElement
{
	// --- 1. 정렬 키 (Sorting Keys) ---
	// 렌더러가 상태 변경을 최소화하기 위해 정렬하는 기준입니다.
	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;
	ID3D11InputLayout* InputLayout = nullptr;

	// 셰이더 파라미터(텍스처, 상수 버퍼)를 제공합니다.
	UMaterialInterface* Material = nullptr;
	// GPU에 바인딩될 정점 버퍼입니다.
	ID3D11Buffer* VertexBuffer = nullptr;

	// GPU에 바인딩될 인덱스 버퍼입니다.
	ID3D11Buffer* IndexBuffer = nullptr;

	// 프리미티브 토폴로지입니다. (TriangleList, LineList 등)
	D3D11_PRIMITIVE_TOPOLOGY PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;


	// --- 2. 드로우 데이터 (Draw Data) ---
	// DrawIndexed() 호출에 직접 사용되는 파라미터입니다.

	// 이 드로우 콜이 렌더링할 인덱스의 수입니다.
	uint32 IndexCount = 0;

	// 인덱스 버퍼에서 읽기 시작할 위치(오프셋)입니다.
	uint32 StartIndex = 0;

	// 정점 버퍼의 시작 정점에 추가할 오프셋입니다. (일반적으로 0)
	uint32 BaseVertexIndex = 0;

	// 정점 버퍼의 스트라이드(Stride)입니다. (정점 1개의 크기)
	uint32 VertexStride = 0;


	// --- 3. 인스턴스 데이터 (Instance Data) ---
	// 드로우 콜마다 고유하게 설정되는 데이터입니다. (정렬 키가 아님)

	// 이 오브젝트의 월드 변환 행렬입니다. (Model Matrix)
	FMatrix WorldMatrix;

	// 피킹(Picking) 등에 사용될 고유 ID입니다.
	uint32 ObjectID = 0;

	// 빌보드나 데칼처럼 머티리얼이 아닌 컴포넌트 인스턴스가
	// 직접 텍스처를 지정해야 할 때 사용합니다.
	ID3D11ShaderResourceView* InstanceShaderResourceView = nullptr;

	// 기즈모 하이라이트, 빌보드 틴트 등 인스턴스별 색상 오버라이드입니다.
	// (기본값으로 흰색(1,1,1,1)을 설정하는 것이 일반적입니다.)
	FLinearColor InstanceColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// GPUSkinning용 Structured Buffer
	ID3D11ShaderResourceView* GPUSkinMatrixSRV = nullptr;
	ID3D11ShaderResourceView* GPUSkinNormalMatrixSRV = nullptr;

	// SubUV 파라미터 (파티클 전용)
	int32 SubImages_Horizontal = 1;
	int32 SubImages_Vertical = 1;
	int32 SubUV_InterpMethod = 0;  // 0=None, 1=LinearBlend

	// --- 기본 생성자 ---
	FMeshBatchElement() = default;


	/**
	 * @brief FMeshBatchElement 정렬을 위한 'less than' 연산자입니다.
	 * TArray::Sort()가 A < B 를 비교하기 위해 이 함수를 호출합니다.
	 * GPU 상태 변경을 최소화하는 순서로 정렬 키를 비교합니다.
	 */
	bool operator<(const FMeshBatchElement& B) const
	{
		const FMeshBatchElement& A = *this; // A는 'this' (자신), B는 비교 대상

		// 1순위: 셰이더 프로그램 (VS, PS)
		if (A.VertexShader != B.VertexShader) return A.VertexShader < B.VertexShader;
		if (A.PixelShader != B.PixelShader) return A.PixelShader < B.PixelShader;

		// 2순위: 셰이더 파라미터 (Material)
		if (A.Material != B.Material) return A.Material < B.Material;

		// 3순위: IA(Input Assembler) 상태 (버퍼, 스트라이드, 토폴로지)
		if (A.VertexBuffer != B.VertexBuffer) return A.VertexBuffer < B.VertexBuffer;
		if (A.IndexBuffer != B.IndexBuffer) return A.IndexBuffer < B.IndexBuffer;
		if (A.VertexStride != B.VertexStride) return A.VertexStride < B.VertexStride;
		if (A.PrimitiveTopology != B.PrimitiveTopology) return A.PrimitiveTopology < B.PrimitiveTopology;

		// 모든 키가 동일하면 순서가 중요하지 않으므로 false 반환 (Stable Sort 보장)
		return false;
	}
};