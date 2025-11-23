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
    virtual ~SParticleViewerWindow();                                                              
                                                                                                   
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
                                                                                                   
    // Load a particle system
    void LoadParticleSystem(const FString& Path);
    void LoadParticleSystem(UParticleSystem* ParticleSystem);

    // Set save path for new particle systems
    void SetSavePath(const FString& InSavePath) { SavePath = InSavePath; }

    // Save current particle system
    void SaveParticleSystem();

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

    // Save path (for new particle systems created from content browser)
    FString SavePath;

    // Selected emitter index for UI
    int32 SelectedEmitterIndex = 0;

    // Selected module for details panel
    class UParticleModule* SelectedModule = nullptr;

    // Layout state                                                                                
    float LeftPanelRatio = 0.25f;   // 25% of width                                                
    float RightPanelRatio = 0.25f;  // 25% of width                                                
    float BottomPanelRatio = 0.3f;  // 30% of Height                                               
                                                                                                   
    // Cached center region for viewport                                                           
    FRect CenterRect;                                                                              
                                                                                                   
    // Window state                                                                                
    bool bIsOpen = true;                                                                           
    bool bInitialPlacementDone = false;                                                            
    bool bRequestFocus = false;                                                                    
                                                                                                   
public:
    bool IsOpen() const { return bIsOpen; }
    void Close() { bIsOpen = false; }
    const FRect& GetViewportRect() const { return CenterRect; }                                                              
};                                