#pragma once
#include "SWindow.h"
#include "Source/Runtime/Engine/SkeletalViewer/ViewerState.h"

class FViewport;
class FViewportClient;
class UWorld;
struct ID3D11Device;

class SSkeletalMeshViewerWindow : public SWindow
{
public:
    SSkeletalMeshViewerWindow();
    virtual ~SSkeletalMeshViewerWindow();

    bool Initialize(float StartX, float StartY, float Width, float Height, UWorld* InWorld, ID3D11Device* InDevice);

    // SWindow overrides
    virtual void OnRender() override;
    virtual void OnUpdate(float DeltaSeconds) override;
    virtual void OnMouseMove(FVector2D MousePos) override;
    virtual void OnMouseDown(FVector2D MousePos, uint32 Button) override;
    virtual void OnMouseUp(FVector2D MousePos, uint32 Button) override;

    void OnRenderViewport();

    // Accessors (active tab)
    FViewport* GetViewport() const { return ActiveState ? ActiveState->Viewport : nullptr; }
    FViewportClient* GetViewportClient() const { return ActiveState ? ActiveState->Client : nullptr; }

    // Load a skeletal mesh into the active tab
    void LoadSkeletalMesh(const FString& Path);

private:
    // Tabs
    void OpenNewTab(const char* Name = "Viewer");
    void CloseTab(int Index);
    
    // viwer를 닫을 때 자동으로 Notifies 정보 저장
    void SaveAllNotifiesOnClose();

private:
    // Per-tab state
    ViewerState* ActiveState = nullptr;
    TArray<ViewerState*> Tabs;
    int ActiveTabIndex = -1;

    // For legacy single-state flows; removed once tabs are stable
    UWorld* World = nullptr;
    ID3D11Device* Device = nullptr;

    // Layout state
    float LeftPanelRatio = 0.25f;   // 25% of width
    float RightPanelRatio = 0.25f;  // 25% of width
    float BottomPanelRatio = 0.4f;  // 40% of Height (Animation Panel)

    // Cached center region used for viewport sizing and input mapping
    FRect CenterRect;

    // Whether we've applied the initial ImGui window placement
    bool bInitialPlacementDone = false;

    // Request focus on first open
    bool bRequestFocus = false;

    // Window open state
    bool bIsOpen = true;
    bool bSavedOnClose = false;

public:
    bool IsOpen() const { return bIsOpen; }
    void Close() { bIsOpen = false; }
    const FRect& GetViewportRect() const { return CenterRect; }

private:
    void UpdateBoneTransformFromSkeleton(ViewerState* State);
    
    void ApplyBoneTransform(ViewerState* State);

    void ExpandToSelectedBone(ViewerState* State, int32 BoneIndex);

    void DrawAnimationPanel(ViewerState* State);

    static constexpr float IconSize = 20.0f;

    UTexture* IconFirstFrame = nullptr;
    UTexture* IconLastFrame = nullptr;
    UTexture* IconPrevFrame = nullptr;
    UTexture* IconNextFrame = nullptr;
    UTexture* IconPlay = nullptr;
    UTexture* IconReversePlay = nullptr;
    UTexture* IconPause = nullptr;
    UTexture* IconRecord = nullptr;
    UTexture* IconRecordActive = nullptr;
    UTexture* IconLoop = nullptr;
    UTexture* IconNoLoop = nullptr;

private:
    // Notify editing state
    int32 SelectedNotifyIndex = -1;
    // Right-click capture for notify insertion
    bool  bHasPendingNotifyAddTime = false;
    float PendingNotifyAddTimeSec = 0.0f;

    // Notify drag state (for moving notifies on timeline)
    int32 DraggingNotifyIndex = -1;
    float DraggingStartMouseX = 0.0f;
    float DraggingOrigTriggerTime = 0.0f;
    bool  bDraggingNotify = false;
};
