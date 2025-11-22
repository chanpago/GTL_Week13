#pragma once
#include "UEContainer.h"
#include <d3d11.h>

enum class EPrimitiveTopology
{
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip
};

enum class EGizmoMode : uint8
{
    Translate,
    Rotate,
    Scale,
    Select
};
enum class EGizmoSpace : uint8
{
    World,
    Local
};

enum class EKeyInput : uint8
{
    // Keyboard Keys
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Shift, Ctrl, Alt,
    Up, Down, Left, Right,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Mouse Buttons
    LeftMouse, RightMouse, MiddleMouse, Mouse4, Mouse5,

    // Special
    Unknown
};

enum class EResourceType : uint8
{
    None,

    StaticMesh,
    SkeletalMesh,
    Quad,
    DynamicMesh,
    Shader,
    Texture,
    Material,
    Sound,
    Animation,


    End
};

enum class EVertexLayoutType : uint8
{
    None,

    PositionColor,
    PositionColorTexturNormal,

    PositionTextBillBoard,
    PositionCollisionDebug,
    PositionBillBoard,

    End,
};

// 에디터에서 설정할 수 있는 디버깅용 뷰 모드
enum class EViewMode : uint32
{
    None,

    VMI_Lit_Phong,          // Phong 조명 (Per-Pixel Full)
    VMI_Lit_Gouraud,        // Gouraud 조명 (Per-Vertex)
    VMI_Lit_Lambert,        // Lambert 조명 (Per-Pixel Diffuse)
    VMI_Unlit,              // 조명 없음
    VMI_WorldNormal,        // World Normal 시각화
    VMI_Wireframe,          // 와이어프레임
    VMI_SceneDepth,         // 깊이 버퍼 시각화

    End,
};

// RHI가 사용할 래스터라이저 상태
enum class ERasterizerMode : uint32
{
    Solid,          // 면으로 채움 (기본값)
    Wireframe,      // 선으로 그림
    Solid_NoCull,   // 면으로 채우고, 뒷면 컬링 안 함(양면 그리기)
    Decal,          // 데칼 전용 상태 (Z-Fighting 방지용 DepthBias를 추가로 줌)
    Shadows,        // 그림자 전용 상태
};

// RHI가 사용할 RTV
enum class ERTVMode : uint32
{
	BackBufferWithDepth,
	BackBufferWithoutDepth,
    SceneIdTarget,
    SceneColorTarget,
    SceneColorTargetWithoutDepth,
    SceneColorTargetWithId, //모델 그릴때만 사용, 후처리는 별도
};

// RHI가 사용하는 텍스쳐들의 SRV
enum class RHI_SRV_Index : uint32
{
	SceneDepth,  // 장면 깊이
    SceneColorSource,
};

enum class RHI_Sampler_Index : uint32
{
    Default,    // 기본 샘플러 (반복, 선형 필터링)
    LinearClamp,
	PointClamp,
    Shadow,
    VSM
};

enum class EPrimitiveType : uint32
{
    None,

    Default,
    Sphere,

    End,
};

enum class EEngineShowFlags : uint64
{
    None = 0,

    // Primitive rendering
    SF_Primitives = 1ull << 0,    // Show/hide all primitive geometry
    SF_StaticMeshes = 1ull << 1,  // Show/hide static mesh actors
    SF_SkeletalMeshes = 1ull << 2,  // Show/hide skeletal mesh actors

    // Debug features
    SF_BillboardText = 1ull << 3, // Show/hide UUID text above objects
    SF_BoundingBoxes = 1ull << 4, // Show/hide collision bounds
    SF_Grid = 1ull << 5,          // Show/hide world grid

    // Lighting
    SF_Lighting = 1ull << 6,      // Enable/disable lighting

    SF_OctreeDebug = 1ull << 7,  // Show/hide octree debug bounds
    SF_BVHDebug = 1ull << 8,  // Show/hide BVH debug bounds
    SF_Culling = 1ull << 9,          // Show/hide world grid

    SF_Decals = 1ull << 10,
    SF_Fog = 1ull << 11,

    SF_FXAA = 1ull << 12,

    SF_TileCulling = 1ull << 13,      // Enable/disable tile-based light culling
    SF_TileCullingDebug = 1ull << 14, // Show/hide tile culling debug visualization

    SF_Billboard = 1ull << 15,
    SF_EditorIcon = 1ull << 16,

    SF_Shadows = 1ull << 17,
    SF_ShadowAntiAliasing = 1ull << 18,

    SF_GPUSkinning = 1ull << 19,
    
    SF_Particle = 1ull << 20,

    // Default enabled flags
    SF_DefaultEnabled = SF_Primitives | SF_StaticMeshes | SF_SkeletalMeshes | SF_Grid | SF_Lighting | SF_Decals |
        SF_Fog | SF_FXAA | SF_Billboard | SF_EditorIcon | SF_Shadows | SF_ShadowAntiAliasing | SF_GPUSkinning | SF_Particle,

    // All flags (for initialization/reset)
    SF_All = 0xFFFFFFFFFFFFFFFFull
};

enum class EViewportLayoutMode
{
    SingleMain,
    FourSplit
};

enum class EShadowAATechnique : uint8
{
    PCF,	// Percentage-Closer Filtering
    VSM		// Variance Shadow Maps
};

// Bit flag operators for EEngineShowFlags
inline EEngineShowFlags operator|(EEngineShowFlags a, EEngineShowFlags b)
{
    return static_cast<EEngineShowFlags>(static_cast<uint64>(a) | static_cast<uint64>(b));
}

inline EEngineShowFlags operator&(EEngineShowFlags a, EEngineShowFlags b)
{
    return static_cast<EEngineShowFlags>(static_cast<uint64>(a) & static_cast<uint64>(b));
}

inline EEngineShowFlags operator~(EEngineShowFlags a)
{
    return static_cast<EEngineShowFlags>(~static_cast<uint64>(a));
}

inline EEngineShowFlags& operator|=(EEngineShowFlags& a, EEngineShowFlags b)
{
    a = a | b;
    return a;
}

inline EEngineShowFlags& operator&=(EEngineShowFlags& a, EEngineShowFlags b)
{
    a = a & b;
    return a;
}

// Helper function to check if a flag is set
inline bool HasShowFlag(EEngineShowFlags flags, EEngineShowFlags flag)
{
    return (flags & flag) != EEngineShowFlags::None;
}

enum class EViewportType : uint8
{
    Perspective,    // 원근 뷰
    Orthographic_Top,     // 상단 직교 뷰
    Orthographic_Bottom,    // 하단 직교 뷰
    Orthographic_Front,   // 정면 직교 뷰
    Orthographic_Left,     // 왼쪽면 직교 뷰 
    Orthographic_Right,   // 오른쪽면 직교 뷰
    Orthographic_Back     // 측면 직교 뷰
};

enum class ECameraProjectionMode
{
    Perspective,
    Orthographic
};

enum class EWorldType : uint8
{
    None = 0,

    Editor,
    Game,

    End,
    PreviewMinimal, 
};

enum class EEmitterRenderType : uint8
{
    Sprite,
    Mesh
};