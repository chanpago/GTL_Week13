#pragma once                                                                                      
#include "../SWindow.h"                                                                               
                                                                                                    
class FViewport;
class FViewportClient;
class UWorld;
struct ID3D11Device;
class UParticleSystem;
class AActor;
class UParticleSystemComponent;                                                                             
                                                                                                   
class SParticleViewerWindow : public SWindow                                                       
{                                                                                                  
public:                                                                                            
    SParticleViewerWindow();                                                                       
    ~SParticleViewerWindow() override;                                                              
                                                                                                   
    bool Initialize(float StartX, float StartY, float Width, float Height, UWorld* InWorld, ID3D11Device* InDevice);                                                                           
                                                                                                   
    // SWindow overrides                                                                           
    virtual void OnRender() override;                                                              
    virtual void OnUpdate(float DeltaSeconds) override;                                            
    virtual void OnMouseMove(FVector2D MousePos) override;                                         
    virtual void OnMouseDown(FVector2D MousePos, uint32 Button) override;                          
    virtual void OnMouseUp(FVector2D MousePos, uint32 Button) override;                            
                                                                                                   
    void OnRenderViewport();                                                                       
                                                                                                   
    // Accessors                                                                                   
    FViewport* GetViewport() const { return Viewport; }                                            
    FViewportClient* GetViewportClient() const { return ViewportClient; }                          
                                                                                                   
    // Create new particle system
    void CreateParticleSystem();
    
    // Load a particle system
    void LoadParticleSystem();
    void LoadParticleSystem(UParticleSystem* ParticleSystem);

    // Set save path for new particle systems
    void SetSavePath(const FString& InSavePath) { SavePath = InSavePath; }

    // Save current particle system
    void SaveParticleSystem();

    // Create new emitter in current particle system
    void CreateNewEmitter();

    // Delete selected emitter
    void DeleteSelectedEmitter();

private:
    // ===== 분리된 렌더링 함수 =====
    void RenderMenuBar();
    void RenderToolbar();
    void RenderViewportPanel(float Width, float Height);
    void RenderEmitterPanel(float Width, float Height);
    void RenderCurveEditor(float Width, float Height);

private:
    ID3D11Device* Device = nullptr;

    // Preview World (별도의 파티클 미리보기용 월드)
    UWorld* PreviewWorld = nullptr;

    // Viewport
    FViewport* Viewport = nullptr;
    FViewportClient* ViewportClient = nullptr;

    // Current particle system
    UParticleSystem* CurrentParticleSystem = nullptr;

    // Preview actor with particle component
    AActor* PreviewActor = nullptr;
    UParticleSystemComponent* PreviewComponent = nullptr;

    std::filesystem::path ParticlePath;
    // Save path (for new particle systems created from content browser)
    FString SavePath;

    // Selected emitter index for UI
    int32 SelectedEmitterIndex = 0;

    // Selected emitter for highlighting and deletion
    class UParticleEmitter* SelectedEmitter = nullptr;

    // Selected module for details panel
    class UParticleModule* SelectedModule = nullptr;

    // Curve Editor state
    float CurveZoom = 10.0f;          // 줌 레벨 (1.0 = 기본, 2.0 = 2배 확대)
    FVector2D CurvePan = FVector2D(5.0f, 5.0f);  // 팬 오프셋 (중심점, 초기값은 왼쪽 아래)
    bool bCurvePanning = false;      // 팬 드래그 중인지
    FVector2D CurvePanStart = FVector2D(0.0f, 0.0f);  // 드래그 시작 위치

    // Curve point dragging
    int32 DraggingPointIndex = -1;   // -1: none, 0: start, 1: mid, 2: end
    bool bDraggingPoint = false;     // 포인트 드래그 중인지

    // Layout state
    float LeftPanelRatio = 0.25f;       // 좌측 패널 너비 비율
    float LeftBottomRatio = 0.4f;       // 좌측 하단(Properties) 높이 비율
    float RightBottomRatio = 0.3f;      // 우측 하단(커브에디터) 높이 비율                                               
                                                                                                   
    // Cached center region for viewport                                                           
    FRect CenterRect;                                                                              
    
    // Window state
    bool bIsOpen = true;
    bool bInitialPlacementDone = false;
    bool bRequestFocus = false;
    bool bPaused = false;                                                                    
                                                                                                   
public:
    bool IsOpen() const { return bIsOpen; }
    void Close() { bIsOpen = false; }
    const FRect& GetViewportRect() const { return CenterRect; }                                                              
};                                