#pragma once
// b0 in VS    
#include "Color.h"
#include "LightManager.h"

struct ModelBufferType // b0
{
    FMatrix Model;
    FMatrix ModelInverseTranspose;  // For correct normal transformation with non-uniform scale
};

struct ViewProjBufferType // b1 고유번호 고정
{
    FMatrix View;
    FMatrix Proj;
    FMatrix InvView;
    FMatrix InvProj;
};

struct DecalBufferType
{
    FMatrix DecalMatrix;
    float Opacity;
};

// Fireball material parameters (b6 in PS)
struct FireballBufferType
{
    float Time;
    FVector2D Resolution;
    float Padding0; // align to 16 bytes

    FVector CameraPosition;
    float Padding1; // align to 16 bytes

    FVector2D UVScrollSpeed;
    FVector2D UVRotateRad;

    uint32 LayerCount;
    float LayerDivBase;
    float GateK;
    float IntensityScale;
};

struct PostProcessBufferType // b0
{
    float Near;
    float Far;
    int IsOrthographic; // 0 = Perspective, 1 = Orthographic
    float Padding; // 16바이트 정렬을 위한 패딩
};

struct FogBufferType // b2
{
    float FogDensity;
    float FogHeightFalloff;
    float StartDistance;
    float FogCutoffDistance;

    FVector4 FogInscatteringColor; // 16 bytes alignment 위해 중간에 넣음

    float FogMaxOpacity;
    float FogHeight; // fog base height
    float Padding[2]; // 16바이트 정렬을 위한 패딩
};

struct alignas(16) FFadeInOutBufferType // b2
{
    FLinearColor FadeColor = FLinearColor(0, 0, 0, 1);  //보통 (0, 0, 0, 1)
    
    float Opacity = 0.0f;   // 0~1
    float Weight  = 1.0f;
    float _Pad[2] = {0,0};
};
static_assert(sizeof(FFadeInOutBufferType) % 16 == 0, "CB must be 16-byte aligned");

struct alignas(16) FVinetteBufferType // b2
{
    FLinearColor Color;
    
    float     Radius    = 0.35f;                        // 효과 시작 반경(0~1)
    float     Softness  = 0.25f;                        // 페더 폭(0~1)
    float     Intensity = 1.0f;                       // 컬러 블렌드 강도 (0~1)
    float     Roundness = 2.0f;                       // 1=마름모, 2= 원형, >1 더 네모에 가깝게
    
    float     Weight    = 1.0f;
    float     _Pad0[3];
};
static_assert(sizeof(FVinetteBufferType) % 16 == 0, "CB must be 16-byte aligned");

struct alignas(16) FGammaCorrectionBufferType
{
    float Gamma;
    float Padding[3];
};
static_assert(sizeof(FGammaCorrectionBufferType) % 16 == 0, "CB must be 16-byte aligned");

// DOF Setup Pass (b2)
struct alignas(16) FDOFSetupBufferType
{
    float FocalDistance;           // m (초점 거리)
    float FocalRegion;             // m (완전 선명 영역)
    float NearTransitionRegion;    // m (근경 블러 전환 영역)
    float FarTransitionRegion;     // m (원경 블러 전환 영역)

    float MaxNearBlurSize;         // pixels (근경 최대 블러)
    float MaxFarBlurSize;          // pixels (원경 최대 블러)
    float NearClip;
    float FarClip;

    int32 IsOrthographic;          // 0 = Perspective, 1 = Orthographic
    FVector _Pad0;
};
static_assert(sizeof(FDOFSetupBufferType) % 16 == 0, "CB must be 16-byte aligned");

// DOF Blur Pass (b2)
struct alignas(16) FDOFBlurBufferType
{
    FVector2D BlurDirection;       // (1,0) = Horizontal, (0,1) = Vertical
    float BlurRadius;              // 블러 반경 스케일
    float _Pad0;
};
static_assert(sizeof(FDOFBlurBufferType) % 16 == 0, "CB must be 16-byte aligned");

// DOF Recombine Pass (b2)
struct alignas(16) FDOFRecombineBufferType
{
    float FocalDistance;           // m (초점 거리)
    float FocalRegion;             // m (완전 선명 영역)
    float NearTransitionRegion;    // m (근경 블러 전환 영역)
    float FarTransitionRegion;     // m (원경 블러 전환 영역)

    float MaxNearBlurSize;         // pixels (근경 최대 블러)
    float MaxFarBlurSize;          // pixels (원경 최대 블러)
    float NearClip;
    float FarClip;

    int32 IsOrthographic;
    float _Pad0;
    FVector2D ViewRectMinUV;       // ViewRect 시작 UV (게임 영역)

    FVector2D ViewRectMaxUV;       // ViewRect 끝 UV (게임 영역)
    FVector2D _Pad1;
};
static_assert(sizeof(FDOFRecombineBufferType) % 16 == 0, "CB must be 16-byte aligned");

struct FXAABufferType // b2
{
    FVector2D InvScreenSize; // 1.0f / ScreenSize (픽셀 하나의 크기)
    float SpanMax;           // 최대 탐색 범위 (8.0f 권장)
    float ReduceMul;         // 감쇠 승수 (1/8 = 0.125f 권장)

    float ReduceMin;         // 최소 감쇠 값 (1/128 = 0.0078125f 권장)
    float SubPixBlend;       // 서브픽셀 블렌딩 강도 (0.75~1.0 권장)
    float Padding[2];        // 16바이트 정렬
};

// b0 in PS
struct FMaterialInPs
{
    FVector DiffuseColor; // Kd
    float OpticalDensity; // Ni

    FVector AmbientColor; // Ka
    float Transparency; // Tr Or d

    FVector SpecularColor; // Ks
    float SpecularExponent; // Ns

    FVector EmissiveColor; // Ke
    uint32 IlluminationModel; // illum. Default illumination model to Phong for non-Pbr materials

    FVector TransmissionFilter; // Tf
    float dummy; // 4 bytes padding
    FMaterialInPs() = default;
    FMaterialInPs(const FMaterialInfo& MaterialInfo)
        :DiffuseColor(MaterialInfo.DiffuseColor),
        OpticalDensity(MaterialInfo.OpticalDensity),
        AmbientColor(MaterialInfo.AmbientColor),
        Transparency(MaterialInfo.Transparency),
        SpecularColor(MaterialInfo.SpecularColor),
        SpecularExponent(MaterialInfo.SpecularExponent),
        EmissiveColor(MaterialInfo.EmissiveColor),
        IlluminationModel(MaterialInfo.IlluminationModel),
        TransmissionFilter(MaterialInfo.TransmissionFilter),
        dummy(0)
    { 

    }
};


struct FPixelConstBufferType
{
    FMaterialInPs Material;
    uint32 bHasMaterial;
    uint32 bHasDiffuseTexture;
    uint32 bHasNormalTexture;
	float Padding; // 16바이트 정렬을 위한 패딩
};

struct ColorBufferType // b3
{
    FLinearColor Color;
    uint32 UUID;
    FVector Padding;
};

struct FLightBufferType
{
    FAmbientLightInfo AmbientLight;
    FDirectionalLightInfo DirectionalLight;

    uint32 PointLightCount;
    uint32 SpotLightCount;
    FVector2D Padding;
};

// b10 고유번호 고정
struct FViewportConstants
{
    // x = Viewport TopLeftX
    // y = Viewport TopLeftY
    // z = Viewport Width
    // w = Viewport Height
    FVector4 ViewportRect;

    // x = Screen Width (전체 렌더 타겟 너비)
    // y = Screen Height (전체 렌더 타겟 높이)
    // z = 1.0f / Screen Width
    // w = 1.0f / Screen Height
    FVector4 ScreenSize;
};

struct CameraBufferType // b7
{
    FVector CameraPosition;
    float Padding;
};

// b11: 타일 기반 라이트 컬링 상수 버퍼
struct FTileCullingBufferType
{
    uint32 TileSize;          // 타일 크기 (픽셀, 기본 16)
    uint32 TileCountX;        // 가로 타일 개수
    uint32 TileCountY;        // 세로 타일 개수
    uint32 bUseTileCulling;   // 타일 컬링 활성화 여부 (0=비활성화, 1=활성화)
    uint32 ViewportStartX;    // 뷰포트 시작 X 좌표
    uint32 ViewportStartY;    // 뷰포트 시작 Y 좌표
    uint32 Padding[2];
};

struct FPointLightShadowBufferType
{
    FMatrix LightViewProjection[6]; // 각 큐브맵 면에 대한 뷰-프로젝션 행렬 (6개)
    FVector LightPosition;          // 라이트의 월드 공간 위치
    float FarPlane;                 // 섀도우 맵의 원거리 평면 (깊이 범위 계산용)
    uint32 LightIndex;              // 현재 렌더링 중인 라이트 인덱스
    FVector Padding;                // 16바이트 정렬
};

// b2: SubUV 파라미터 (파티클 스프라이트 시트 애니메이션용)
struct FSubUVBufferType
{
    int32 SubImages_Horizontal;  // NX (가로 타일 수)
    int32 SubImages_Vertical;    // NY (세로 타일 수)
    int32 InterpMethod;          // 0=None, 1=LinearBlend
    float Padding0;              // 16바이트 정렬
};

// b3: 파티클 이미터 파라미터
struct FParticleEmitterType
{
    uint32 ScreenAlignment;  // Screen Alignment (0 - Camera, 1 - Velocity)
    FVector Padding0;        // 16바이트 정렬
};

#define CONSTANT_BUFFER_INFO(TYPE, SLOT, VS, PS) \
constexpr uint32 TYPE##Slot = SLOT;\
constexpr bool TYPE##IsVS = VS;\
constexpr bool TYPE##IsPS = PS;

//매크로를 인자로 받고 그 매크로 함수에 버퍼 전달
#define CONSTANT_BUFFER_LIST(MACRO) \
MACRO(ModelBufferType)              \
MACRO(DecalBufferType)              \
MACRO(FireballBufferType)           \
MACRO(PostProcessBufferType)        \
MACRO(FogBufferType)                \
MACRO(FFadeInOutBufferType)         \
MACRO(FGammaCorrectionBufferType)   \
MACRO(FVinetteBufferType)           \
MACRO(FXAABufferType)               \
MACRO(FDOFSetupBufferType)          \
MACRO(FDOFBlurBufferType)           \
MACRO(FDOFRecombineBufferType)      \
MACRO(FPixelConstBufferType)        \
MACRO(ViewProjBufferType)           \
MACRO(ColorBufferType)              \
MACRO(CameraBufferType)             \
MACRO(FLightBufferType)             \
MACRO(FViewportConstants)           \
MACRO(FTileCullingBufferType)       \
MACRO(FPointLightShadowBufferType)  \
MACRO(FSubUVBufferType) \
MACRO(FParticleEmitterType)

// 16 바이트 패딩 어썰트
#define STATIC_ASSERT_CBUFFER_ALIGNMENT(Type) \
    static_assert(sizeof(Type) % 16 == 0, "[ " #Type " ] Bad Size. Needs 16-Byte Padding.");
CONSTANT_BUFFER_LIST(STATIC_ASSERT_CBUFFER_ALIGNMENT)

//VS, PS 세팅은 함수 파라미터로 결정하게 하는게 훨씬 나을듯 나중에 수정 필요
//그리고 UV Scroll 상수버퍼도 처리해줘야함
CONSTANT_BUFFER_INFO(ModelBufferType, 0, true, false)
CONSTANT_BUFFER_INFO(PostProcessBufferType, 0, false, true)
CONSTANT_BUFFER_INFO(ViewProjBufferType, 1, true, true) // b1 카메라 행렬 고정
CONSTANT_BUFFER_INFO(FogBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FFadeInOutBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FGammaCorrectionBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FVinetteBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FXAABufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FDOFSetupBufferType, 2, false, true)      // b2, PS only (DOF Setup Pass)
CONSTANT_BUFFER_INFO(FDOFBlurBufferType, 2, false, true)       // b2, PS only (DOF Blur Pass)
CONSTANT_BUFFER_INFO(FDOFRecombineBufferType, 2, false, true)  // b2, PS only (DOF Recombine Pass)
CONSTANT_BUFFER_INFO(ColorBufferType, 3, true, true)   // b3 color
CONSTANT_BUFFER_INFO(FPixelConstBufferType, 4, true, true) // GOURAUD에도 사용되므로 VS도 true
CONSTANT_BUFFER_INFO(DecalBufferType, 6, true, true)
CONSTANT_BUFFER_INFO(FireballBufferType, 6, false, true)
CONSTANT_BUFFER_INFO(CameraBufferType, 7, true, true)  // b7, VS+PS (UberLit.hlsl과 일치)
CONSTANT_BUFFER_INFO(FLightBufferType, 8, true, true)
CONSTANT_BUFFER_INFO(FViewportConstants, 10, true, true)   // 뷰 포트 크기에 따라 전체 화면 복사를 보정하기 위해 설정 (10번 고유번호로 사용)
CONSTANT_BUFFER_INFO(FTileCullingBufferType, 11, false, true)  // b11, PS only (UberLit.hlsl과 일치)
CONSTANT_BUFFER_INFO(FPointLightShadowBufferType, 12, true, true)  // b12, VS+PS
CONSTANT_BUFFER_INFO(FSubUVBufferType, 2, true, true)  // b2, VS+PS (ParticleSprite.hlsl용)
CONSTANT_BUFFER_INFO(FParticleEmitterType, 3, true, false)  // b3, VS (ParticleSprite.hlsl용)



