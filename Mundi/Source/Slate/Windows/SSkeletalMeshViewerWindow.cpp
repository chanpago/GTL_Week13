#include "pch.h"
#include "SSkeletalMeshViewerWindow.h"
#include "FViewport.h"
#include "FViewportClient.h"
#include "Source/Runtime/Engine/SkeletalViewer/SkeletalViewerBootstrap.h"
#include "Source/Editor/PlatformProcess.h"
#include "Source/Runtime/Engine/GameFramework/SkeletalMeshActor.h"
#include "Source/Runtime/Engine/Components/LineComponent.h"
#include "SelectionManager.h"
#include "USlateManager.h"
#include "BoneAnchorComponent.h"
#include "SkinningStats.h"
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/Engine/Collision/Picking.h"
#include "Source/Runtime/Engine/Animation/AnimNotify_PlaySound.h"
#include "Source/Runtime/AssetManagement/ResourceManager.h"
#include "Source/Editor/PlatformProcess.h"
#include <filesystem>
#include "Source/Runtime/Engine/GameFramework/CameraActor.h" "
SSkeletalMeshViewerWindow::SSkeletalMeshViewerWindow()
{
    CenterRect = FRect(0, 0, 0, 0);
    
    IconFirstFrame = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsToFront.png");
    IconLastFrame = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsToEnd.png");
    IconPrevFrame = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsToPrevious.png");
    IconNextFrame = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsToNext.png");
    IconPlay = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsPlayForward.png");
    IconReversePlay = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsPlayReverse.png");
    IconPause = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsPause.png");
    IconRecord = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsRecord.png");
    IconRecordActive = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsRecord.png");
    IconLoop = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsLooping.png");
    IconNoLoop = UResourceManager::GetInstance().Load<UTexture>("Data/Icon/PlayControlsNoLooping.png");
}

SSkeletalMeshViewerWindow::~SSkeletalMeshViewerWindow()
{
    // Clean up tabs if any
    for (int i = 0; i < Tabs.Num(); ++i)
    {
        ViewerState* State = Tabs[i];
        SkeletalViewerBootstrap::DestroyViewerState(State);
    }
    Tabs.Empty();
    ActiveState = nullptr;
}

bool SSkeletalMeshViewerWindow::Initialize(float StartX, float StartY, float Width, float Height, UWorld* InWorld, ID3D11Device* InDevice)
{
    World = InWorld;
    Device = InDevice;
    
    SetRect(StartX, StartY, StartX + Width, StartY + Height);

    // Create first tab/state
    OpenNewTab("Viewer 1");
    if (ActiveState && ActiveState->Viewport)
    {
        ActiveState->Viewport->Resize((uint32)StartX, (uint32)StartY, (uint32)Width, (uint32)Height);
    }

    bRequestFocus = true;
    return true;
}

void SSkeletalMeshViewerWindow::OnRender()
{
    // If window is closed, don't render
    if (!bIsOpen)
    {
        return;
    }

    // Parent detachable window (movable, top-level) with solid background
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;

    if (!bInitialPlacementDone)
    {
        ImGui::SetNextWindowPos(ImVec2(Rect.Left, Rect.Top));
        ImGui::SetNextWindowSize(ImVec2(Rect.GetWidth(), Rect.GetHeight()));
        bInitialPlacementDone = true;
    }

    if (bRequestFocus)
    {
        ImGui::SetNextWindowFocus();
    }
    bool bViewerVisible = false;
    if (ImGui::Begin("Skeletal Mesh Viewer", &bIsOpen, flags))
    {
        bViewerVisible = true;
        // Render tab bar and switch active state
        if (ImGui::BeginTabBar("SkeletalViewerTabs", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable))
        {
            for (int i = 0; i < Tabs.Num(); ++i)
            {
                ViewerState* State = Tabs[i];
                bool open = true;
                if (ImGui::BeginTabItem(State->Name.ToString().c_str(), &open))
                {
                    ActiveTabIndex = i;
                    ActiveState = State;
                    ImGui::EndTabItem();
                }
                if (!open)
                {
                    CloseTab(i);
                    break;
                }
            }
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
            {
                char label[32]; sprintf_s(label, "Viewer %d", Tabs.Num() + 1);
                OpenNewTab(label);
            }
            ImGui::EndTabBar();
        }
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        Rect.Left = pos.x; Rect.Top = pos.y; Rect.Right = pos.x + size.x; Rect.Bottom = pos.y + size.y; Rect.UpdateMinMax();

        ImVec2 contentAvail = ImGui::GetContentRegionAvail();
        float totalWidth = contentAvail.x;
        float totalHeight = contentAvail.y;

        float leftWidth = totalWidth * LeftPanelRatio;
        float rightWidth = totalWidth * RightPanelRatio;
        float centerWidth = totalWidth - leftWidth - rightWidth;

        // 상단 패널(뷰포트, 속성)과 하단 패널(타임라인, 브라우저)의 높이를 미리 계산합니다.
        const float BottomPanelHeight = totalHeight * BottomPanelRatio;
        float TopPanelHeight = totalHeight - BottomPanelHeight;

        // 패널 사이의 간격 조정
        if (ImGui::GetStyle().ItemSpacing.y > 0)
        {
            TopPanelHeight -= ImGui::GetStyle().ItemSpacing.y;
        }
        // 최소 높이 보장
        TopPanelHeight = std::max(TopPanelHeight, 50.0f);

        // Remove spacing between panels
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        // Left panel - Asset Browser & Bone Hierarchy
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::BeginChild("LeftPanel", ImVec2(leftWidth, totalHeight), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        if (ActiveState)
        {
            // Asset Browser Section
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.35f, 0.50f, 0.8f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            ImGui::Indent(8.0f);
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            ImGui::Text("Asset Browser");
            ImGui::PopFont();
            ImGui::Unindent(8.0f);
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.45f, 0.60f, 0.7f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Mesh path section
            ImGui::BeginGroup();
            ImGui::Text("Mesh Path:");
            ImGui::PushItemWidth(-1.0f);
            ImGui::InputTextWithHint("##MeshPath", "Browse for FBX file...", ActiveState->MeshPathBuffer, sizeof(ActiveState->MeshPathBuffer));
            ImGui::PopItemWidth();

            ImGui::Spacing();

            // Buttons
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.40f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.50f, 0.70f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.35f, 0.50f, 1.0f));

            float buttonWidth = (leftWidth - 24.0f) * 0.5f - 4.0f;
            if (ImGui::Button("Browse...", ImVec2(buttonWidth, 32)))
            {
                auto widePath = FPlatformProcess::OpenLoadFileDialog(UTF8ToWide(GDataDir), L"fbx", L"FBX Files");
                if (!widePath.empty())
                {
                    std::string s = widePath.string();
                    strncpy_s(ActiveState->MeshPathBuffer, s.c_str(), sizeof(ActiveState->MeshPathBuffer) - 1);
                }
            }

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.70f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.50f, 0.30f, 1.0f));
            if (ImGui::Button("Load FBX", ImVec2(buttonWidth, 32)))
            {
                FString Path = ActiveState->MeshPathBuffer;
                if (!Path.empty())
                {
                    USkeletalMesh* Mesh = UResourceManager::GetInstance().Load<USkeletalMesh>(Path);
                    if (Mesh && ActiveState->PreviewActor)
                    {
                        ActiveState->PreviewActor->SetSkeletalMesh(Path);
                        ActiveState->CurrentMesh = Mesh;
                        ActiveState->LoadedMeshPath = Path;  // Track for resource unloading
                        if (auto* Skeletal = ActiveState->PreviewActor->GetSkeletalMeshComponent())
                        {
                            Skeletal->SetVisibility(ActiveState->bShowMesh);
                        }
                        ActiveState->bBoneLinesDirty = true;
                        if (auto* LineComp = ActiveState->PreviewActor->GetBoneLineComponent())
                        {
                            LineComp->ClearLines();
                            LineComp->SetLineVisible(ActiveState->bShowBones);
                        }
                    }
                }
            }
            ImGui::PopStyleColor(6);
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.45f, 0.60f, 0.7f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Display Options
            ImGui::BeginGroup();
            ImGui::Text("Display Options:");
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.30f, 0.35f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.40f, 0.70f, 1.00f, 1.0f));

            if (ImGui::Checkbox("Show Mesh", &ActiveState->bShowMesh))
            {
                if (ActiveState->PreviewActor && ActiveState->PreviewActor->GetSkeletalMeshComponent())
                {
                    ActiveState->PreviewActor->GetSkeletalMeshComponent()->SetVisibility(ActiveState->bShowMesh);
                }
            }

            ImGui::SameLine();
            if (ImGui::Checkbox("Show Bones", &ActiveState->bShowBones))
            {
                if (ActiveState->PreviewActor && ActiveState->PreviewActor->GetBoneLineComponent())
                {
                    ActiveState->PreviewActor->GetBoneLineComponent()->SetLineVisible(ActiveState->bShowBones);
                }
                if (ActiveState->bShowBones)
                {
                    ActiveState->bBoneLinesDirty = true;
                }
            }
            ImGui::PopStyleColor(2);
            ImGui::EndGroup();           

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.45f, 0.60f, 0.7f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Bone Hierarchy Section
            ImGui::Text("Bone Hierarchy:");
            ImGui::Spacing();

            if (!ActiveState->CurrentMesh)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("No skeletal mesh loaded.");
                ImGui::PopStyleColor();
            }
            else
            {
                const FSkeleton* Skeleton = ActiveState->CurrentMesh->GetSkeleton();
                if (!Skeleton || Skeleton->Bones.IsEmpty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::TextWrapped("This mesh has no skeleton data.");
                    ImGui::PopStyleColor();
                }
                else
                {
                    // Scrollable tree view
                    ImGui::BeginChild("BoneTreeView", ImVec2(0, 0), true);
                    const TArray<FBone>& Bones = Skeleton->Bones;
                    TArray<TArray<int32>> Children;
                    Children.resize(Bones.size());
                    for (int32 i = 0; i < Bones.size(); ++i)
                    {
                        int32 Parent = Bones[i].ParentIndex;
                        if (Parent >= 0 && Parent < Bones.size())
                        {
                            Children[Parent].Add(i);
                        }
                    }

                    std::function<void(int32)> DrawNode = [&](int32 Index)
                    {
                        const bool bLeaf = Children[Index].IsEmpty();
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
                        
                        if (bLeaf)
                        {
                            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        }
                        
                        if (ActiveState->ExpandedBoneIndices.count(Index) > 0)
                        {
                            ImGui::SetNextItemOpen(true);
                        }
                        
                        if (ActiveState->SelectedBoneIndex == Index)
                        {
                            flags |= ImGuiTreeNodeFlags_Selected;
                        }

                        ImGui::PushID(Index);
                        const char* Label = Bones[Index].Name.c_str();

                        if (ActiveState->SelectedBoneIndex == Index)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.55f, 0.85f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.40f, 0.60f, 0.90f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.30f, 0.50f, 0.80f, 1.0f));
                        }

                        bool open = ImGui::TreeNodeEx((void*)(intptr_t)Index, flags, "%s", Label ? Label : "<noname>");

                        if (ActiveState->SelectedBoneIndex == Index)
                        {
                            ImGui::PopStyleColor(3);
                            ImGui::SetScrollHereY(0.5f);
                        }

                        if (ImGui::IsItemToggledOpen())
                        {
                            if (open)
                                ActiveState->ExpandedBoneIndices.insert(Index);
                            else
                                ActiveState->ExpandedBoneIndices.erase(Index);
                        }

                        if (ImGui::IsItemClicked())
                        {
                            if (ActiveState->SelectedBoneIndex != Index)
                            {
                                ActiveState->SelectedBoneIndex = Index;
                                ActiveState->bBoneLinesDirty = true;
                                
                                ExpandToSelectedBone(ActiveState, Index);

                                if (ActiveState->PreviewActor && ActiveState->World)
                                {
                                    ActiveState->PreviewActor->RepositionAnchorToBone(Index);
                                    if (USceneComponent* Anchor = ActiveState->PreviewActor->GetBoneGizmoAnchor())
                                    {
                                        ActiveState->World->GetSelectionManager()->SelectActor(ActiveState->PreviewActor);
                                        ActiveState->World->GetSelectionManager()->SelectComponent(Anchor);
                                    }
                                }
                            }
                        }
                        
                        if (!bLeaf && open)
                        {
                            for (int32 Child : Children[Index])
                            {
                                DrawNode(Child);
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    };

                    for (int32 i = 0; i < Bones.size(); ++i)
                    {
                        if (Bones[i].ParentIndex < 0)
                        {
                            DrawNode(i);
                        }
                    }

                    ImGui::EndChild();
                }
            }
        }
        else
        {
            ImGui::EndChild();
            ImGui::End();
            return;
        }
        ImGui::EndChild();

        ImGui::SameLine(0, 0); // No spacing between panels

        // Center panel (viewport area)
        ImGui::BeginChild("CenterColumn", ImVec2(centerWidth, totalHeight), false, ImGuiWindowFlags_NoScrollbar);
        {
            // 상단: 뷰포트 자식 창: 계산된 TopPanelHeight 사용
            ImGui::BeginChild("SkeletalMeshViewport", ImVec2(0, TopPanelHeight), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 childPos = ImGui::GetWindowPos();
                ImVec2 childSize = ImGui::GetWindowSize();
                ImVec2 rectMin = childPos;
                ImVec2 rectMax(childPos.x + childSize.x, childPos.y + childSize.y);
                CenterRect.Left = rectMin.x; CenterRect.Top = rectMin.y; CenterRect.Right = rectMax.x; CenterRect.Bottom = rectMax.y; CenterRect.UpdateMinMax();
            }
            ImGui::EndChild();

            ImGui::Separator();

            // 하단: 애니메이션 패널: 남은 공간(BottomPanelHeight)을 채움
            ImGui::BeginChild("AnimationPanel", ImVec2(0, 0), true);
            {
                if (ActiveState)
                {
                    DrawAnimationPanel(ActiveState);
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::SameLine(0, 0); // No spacing between panels

        // Right panel - Bone Properties & Anim Browser
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        // 1. RightPanel '컨테이너' 자식 창
        ImGui::BeginChild("RightPanel", ImVec2(rightWidth, totalHeight), true);
        ImGui::PopStyleVar();
        {
            // 2. 상단: "Bone Properties"를 위한 자식 창 (TopPanelHeight 사용)
            ImGui::BeginChild("BonePropertiesChild", ImVec2(0, TopPanelHeight), false); 
            {
                // Panel header
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.35f, 0.50f, 0.8f));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                ImGui::Indent(8.0f);
                ImGui::Text("Bone Properties");
                ImGui::Unindent(8.0f);
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.45f, 0.60f, 0.7f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // === 선택된 본의 트랜스폼 편집 UI ===
                if (ActiveState->SelectedBoneIndex >= 0 && ActiveState->CurrentMesh)
                {
                    const FSkeleton* Skeleton = ActiveState->CurrentMesh->GetSkeleton();
                    if (Skeleton && ActiveState->SelectedBoneIndex < Skeleton->Bones.size())
                    {
                        const FBone& SelectedBone = Skeleton->Bones[ActiveState->SelectedBoneIndex];

                        // Selected bone header with icon-like prefix
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.90f, 0.40f, 1.0f));
                        ImGui::Text("> Selected Bone");
                        ImGui::PopStyleColor();

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.95f, 1.00f, 1.0f));
                        ImGui::TextWrapped("%s", SelectedBone.Name.c_str());
                        ImGui::PopStyleColor();

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.55f, 0.70f, 0.8f));
                        ImGui::Separator();
                        ImGui::PopStyleColor();

                        // 본의 현재 트랜스폼 가져오기 (편집 중이 아닐 때만)
                        if (!ActiveState->bBoneRotationEditing)
                        {
                            UpdateBoneTransformFromSkeleton(ActiveState);
                        }

                        ImGui::Spacing();

                        // Location 편집
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                        ImGui::Text("Location");
                        ImGui::PopStyleColor();

                        ImGui::PushItemWidth(-1);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.20f, 0.20f, 0.6f));
                        bool bLocationChanged = false;
                        bLocationChanged |= ImGui::DragFloat("##BoneLocX", &ActiveState->EditBoneLocation.X, 0.1f, 0.0f, 0.0f, "X: %.3f");
                        bLocationChanged |= ImGui::DragFloat("##BoneLocY", &ActiveState->EditBoneLocation.Y, 0.1f, 0.0f, 0.0f, "Y: %.3f");
                        bLocationChanged |= ImGui::DragFloat("##BoneLocZ", &ActiveState->EditBoneLocation.Z, 0.1f, 0.0f, 0.0f, "Z: %.3f");
                        ImGui::PopStyleColor();
                        ImGui::PopItemWidth();

                        if (bLocationChanged)
                        {
                            ApplyBoneTransform(ActiveState);
                            ActiveState->bBoneLinesDirty = true;
                        }

                        ImGui::Spacing();

                        // Rotation 편집
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
                        ImGui::Text("Rotation");
                        ImGui::PopStyleColor();

                        ImGui::PushItemWidth(-1);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.28f, 0.20f, 0.6f));
                        bool bRotationChanged = false;

                        if (ImGui::IsAnyItemActive())
                        {
                            ActiveState->bBoneRotationEditing = true;
                        }

                        bRotationChanged |= ImGui::DragFloat("##BoneRotX", &ActiveState->EditBoneRotation.X, 0.5f, -180.0f, 180.0f, "X: %.2f°");
                        bRotationChanged |= ImGui::DragFloat("##BoneRotY", &ActiveState->EditBoneRotation.Y, 0.5f, -180.0f, 180.0f, "Y: %.2f°");
                        bRotationChanged |= ImGui::DragFloat("##BoneRotZ", &ActiveState->EditBoneRotation.Z, 0.5f, -180.0f, 180.0f, "Z: %.2f°");
                        ImGui::PopStyleColor();
                        ImGui::PopItemWidth();

                        if (!ImGui::IsAnyItemActive())
                        {
                            ActiveState->bBoneRotationEditing = false;
                        }

                        if (bRotationChanged)
                        {
                            ApplyBoneTransform(ActiveState);
                            ActiveState->bBoneLinesDirty = true;
                        }

                        ImGui::Spacing();

                        // Scale 편집
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 1.0f, 1.0f));
                        ImGui::Text("Scale");
                        ImGui::PopStyleColor();

                        ImGui::PushItemWidth(-1);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.20f, 0.28f, 0.6f));
                        bool bScaleChanged = false;
                        bScaleChanged |= ImGui::DragFloat("##BoneScaleX", &ActiveState->EditBoneScale.X, 0.01f, 0.001f, 100.0f, "X: %.3f");
                        bScaleChanged |= ImGui::DragFloat("##BoneScaleY", &ActiveState->EditBoneScale.Y, 0.01f, 0.001f, 100.0f, "Y: %.3f");
                        bScaleChanged |= ImGui::DragFloat("##BoneScaleZ", &ActiveState->EditBoneScale.Z, 0.01f, 0.001f, 100.0f, "Z: %.3f");
                        ImGui::PopStyleColor();
                        ImGui::PopItemWidth();

                        if (bScaleChanged)
                        {
                            ApplyBoneTransform(ActiveState);
                            ActiveState->bBoneLinesDirty = true;
                        }
                    }
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::TextWrapped("Select a bone from the hierarchy to edit its transform properties.");
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild(); // "BonePropertiesChild"

            ImGui::Separator();

            // 3. 하단: "Animation Browser"를 위한 자식 창 (남은 공간 모두 사용)
            ImGui::BeginChild("AnimBrowserChild", ImVec2(0, 0), true);
            {
                // --- Animation Browser 헤더 ---
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.50f, 0.35f, 0.8f));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                ImGui::Indent(8.0f);
                ImGui::Text("Animation Browser");
                ImGui::Unindent(8.0f);
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.60f, 0.45f, 0.7f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // --- 에셋 리스트 (스크롤 가능) ---
                TArray<UAnimSequence*> AnimSequences = UResourceManager::GetInstance().GetAll<UAnimSequence>();

                if (AnimSequences.IsEmpty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::TextWrapped("No animation sequences found.");
                    ImGui::PopStyleColor();
                }
                else
                {
                    for (UAnimSequence* Anim : AnimSequences)
                    {
                        if (!Anim) continue;

                        FString AssetName = Anim->GetFilePath();
                        size_t lastSlash = AssetName.find_last_of("/\\");
                        if (lastSlash != FString::npos)
                        {
                            AssetName = AssetName.substr(lastSlash + 1);
                        }

                        bool bIsSelected = (ActiveState->CurrentAnimation == Anim);

                        if (ImGui::Selectable(AssetName.c_str(), bIsSelected))
                        {
                            if (ActiveState->PreviewActor && ActiveState->PreviewActor->GetSkeletalMeshComponent())
                            {
                                ActiveState->PreviewActor->GetSkeletalMeshComponent()->SetAnimation(Anim);
                            }
                            ActiveState->CurrentAnimation = Anim;
                            ActiveState->CurrentAnimTime = 0.0f;
                            ActiveState->bIsPlaying = false;
                            ActiveState->bIsPlayingReverse = false;
                        }
                    }
                }
            }
            ImGui::EndChild(); // "AnimBrowserChild"
        }
        ImGui::EndChild(); // "RightPanel"

        // Pop the ItemSpacing style
        ImGui::PopStyleVar();
    }
    ImGui::End();

    // If collapsed or not visible, clear the center rect so we don't render a floating viewport
    if (!bViewerVisible)
    {
        CenterRect = FRect(0, 0, 0, 0);
        CenterRect.UpdateMinMax();
    }

    // If window was closed via X button, notify the manager to clean up
    if (!bIsOpen)
    {
        USlateManager::GetInstance().CloseSkeletalMeshViewer();
    }

    bRequestFocus = false;
}

void SSkeletalMeshViewerWindow::OnUpdate(float DeltaSeconds)
{
    if (!ActiveState || !ActiveState->Viewport)
        return;

    if (ActiveState && ActiveState->Client)
    {
        ActiveState->Client->Tick(DeltaSeconds);
    }

    if (ActiveState->bTimeChanged)
    {
        ActiveState->bBoneLinesDirty = true;
    }

    if (!ActiveState->CurrentAnimation || !ActiveState->CurrentAnimation->GetDataModel())
    {
        if (ActiveState->World)
        {
            ActiveState->World->Tick(DeltaSeconds);
            if (ActiveState->World->GetGizmoActor())
                ActiveState->World->GetGizmoActor()->ProcessGizmoModeSwitch();
        }
        
        if(ActiveState->bTimeChanged)
        {
             ActiveState->bTimeChanged = false;
        }
        return;
    }

    UAnimDataModel* DataModel = nullptr;
    if (ActiveState->CurrentAnimation)
    {
        DataModel = ActiveState->CurrentAnimation->GetDataModel();
    }

    bool bIsPlaying = ActiveState->bIsPlaying || ActiveState->bIsPlayingReverse;
    bool bTimeAdvancedThisFrame = false;

    if (DataModel && DataModel->GetPlayLength() > 0.0f)
    {
        if (ActiveState->bIsPlaying)
        {
            ActiveState->CurrentAnimTime += DeltaSeconds;
            if (ActiveState->CurrentAnimTime > DataModel->GetPlayLength())
            {
                if (ActiveState->bIsLooping)
                {
                    ActiveState->CurrentAnimTime = std::fmodf(ActiveState->CurrentAnimTime, DataModel->GetPlayLength());
                }
                else
                {
                    ActiveState->CurrentAnimTime = DataModel->GetPlayLength();
                    ActiveState->bIsPlaying = false;
                }
            }
            bTimeAdvancedThisFrame = true;
        }
        else if (ActiveState->bIsPlayingReverse)
        {
            ActiveState->CurrentAnimTime -= DeltaSeconds;
            if (ActiveState->CurrentAnimTime < 0.0f) 
            {
                if (ActiveState->bIsLooping)
                {
                    ActiveState->CurrentAnimTime += DataModel->GetPlayLength();
                }
                else
                {
                    ActiveState->CurrentAnimTime = 0.0f;
                    ActiveState->bIsPlayingReverse = false;
                }
            }
            bTimeAdvancedThisFrame = true;
        }
    }

    bool bUpdateAnimation = bTimeAdvancedThisFrame || ActiveState->bTimeChanged;

    if (ActiveState->PreviewActor && ActiveState->PreviewActor->GetSkeletalMeshComponent())
    {
        auto SkeletalMeshComponent = ActiveState->PreviewActor->GetSkeletalMeshComponent();
        
        SkeletalMeshComponent->SetPlaying(bIsPlaying);
        SkeletalMeshComponent->SetLooping(ActiveState->bIsLooping);

        if (bUpdateAnimation)
        {
            bool OriginalPlaying = SkeletalMeshComponent->IsPlaying();
            if (ActiveState->bTimeChanged) 
            {
                SkeletalMeshComponent->SetPlaying(true);
            }
            
            SkeletalMeshComponent->SetAnimationTime(ActiveState->CurrentAnimTime);

            if (ActiveState->bTimeChanged) 
            {
                SkeletalMeshComponent->SetPlaying(OriginalPlaying);
                ActiveState->bTimeChanged = false; 
            }

            ActiveState->bBoneLinesDirty = true;
        }
    }

    if (ActiveState->World)
    {
        ActiveState->World->Tick(DeltaSeconds);
        if (ActiveState->World->GetGizmoActor())
            ActiveState->World->GetGizmoActor()->ProcessGizmoModeSwitch();
    }
}

void SSkeletalMeshViewerWindow::OnMouseMove(FVector2D MousePos)
{
    if (!ActiveState || !ActiveState->Viewport) return;

    if (CenterRect.Contains(MousePos))
    {
        FVector2D LocalPos = MousePos - FVector2D(CenterRect.Left, CenterRect.Top);
        ActiveState->Viewport->ProcessMouseMove((int32)LocalPos.X, (int32)LocalPos.Y);
    }
}

void SSkeletalMeshViewerWindow::OnMouseDown(FVector2D MousePos, uint32 Button)
{
    if (!ActiveState || !ActiveState->Viewport) return;

    if (CenterRect.Contains(MousePos))
    {
        FVector2D LocalPos = MousePos - FVector2D(CenterRect.Left, CenterRect.Top);

        // First, always try gizmo picking (pass to viewport)
        ActiveState->Viewport->ProcessMouseButtonDown((int32)LocalPos.X, (int32)LocalPos.Y, (int32)Button);

        // Left click: if no gizmo was picked, try bone picking
        if (Button == 0 && ActiveState->PreviewActor && ActiveState->CurrentMesh && ActiveState->Client && ActiveState->World)
        {
            // Check if gizmo was picked by checking selection
            UActorComponent* SelectedComp = ActiveState->World->GetSelectionManager()->GetSelectedComponent();

            // Only do bone picking if gizmo wasn't selected
            if (!SelectedComp || !Cast<UBoneAnchorComponent>(SelectedComp))
            {
                // Get camera from viewport client
                ACameraActor* Camera = ActiveState->Client->GetCamera();
                if (Camera)
                {
                    // Get camera vectors
                    FVector CameraPos = Camera->GetActorLocation();
                    FVector CameraRight = Camera->GetRight();
                    FVector CameraUp = Camera->GetUp();
                    FVector CameraForward = Camera->GetForward();

                    // Calculate viewport-relative mouse position
                    FVector2D ViewportMousePos(MousePos.X - CenterRect.Left, MousePos.Y - CenterRect.Top);
                    FVector2D ViewportSize(CenterRect.GetWidth(), CenterRect.GetHeight());

                    // Generate ray from mouse position
                    FRay Ray = MakeRayFromViewport(
                        Camera->GetViewMatrix(),
                        Camera->GetProjectionMatrix(CenterRect.GetWidth() / CenterRect.GetHeight(), ActiveState->Viewport),
                        CameraPos,
                        CameraRight,
                        CameraUp,
                        CameraForward,
                        ViewportMousePos,
                        ViewportSize
                    );

                    // Try to pick a bone
                    float HitDistance;
                    int32 PickedBoneIndex = ActiveState->PreviewActor->PickBone(Ray, HitDistance);

                    if (PickedBoneIndex >= 0)
                    {
                        // Bone was picked
                        ActiveState->SelectedBoneIndex = PickedBoneIndex;
                        ActiveState->bBoneLinesDirty = true;

                        ExpandToSelectedBone(ActiveState, PickedBoneIndex);

                        // Move gizmo to the selected bone
                        ActiveState->PreviewActor->RepositionAnchorToBone(PickedBoneIndex);
                        if (USceneComponent* Anchor = ActiveState->PreviewActor->GetBoneGizmoAnchor())
                        {
                            ActiveState->World->GetSelectionManager()->SelectActor(ActiveState->PreviewActor);
                            ActiveState->World->GetSelectionManager()->SelectComponent(Anchor);
                        }
                    }
                    else
                    {
                        // No bone was picked - clear selection
                        ActiveState->SelectedBoneIndex = -1;
                        ActiveState->bBoneLinesDirty = true;

                        // Hide gizmo and clear selection
                        if (UBoneAnchorComponent* Anchor = ActiveState->PreviewActor->GetBoneGizmoAnchor())
                        {
                            Anchor->SetVisibility(false);
                            Anchor->SetEditability(false);
                        }
                        ActiveState->World->GetSelectionManager()->ClearSelection();
                    }
                }
            }
        }
    }
}

void SSkeletalMeshViewerWindow::OnMouseUp(FVector2D MousePos, uint32 Button)
{
    if (!ActiveState || !ActiveState->Viewport) return;

    if (CenterRect.Contains(MousePos))
    {
        FVector2D LocalPos = MousePos - FVector2D(CenterRect.Left, CenterRect.Top);
        ActiveState->Viewport->ProcessMouseButtonUp((int32)LocalPos.X, (int32)LocalPos.Y, (int32)Button);
    }
}

void SSkeletalMeshViewerWindow::OnRenderViewport()
{
    if (ActiveState && ActiveState->Viewport && CenterRect.GetWidth() > 0 && CenterRect.GetHeight() > 0)
    {
        const uint32 NewStartX = static_cast<uint32>(CenterRect.Left);
        const uint32 NewStartY = static_cast<uint32>(CenterRect.Top);
        const uint32 NewWidth  = static_cast<uint32>(CenterRect.Right - CenterRect.Left);
        const uint32 NewHeight = static_cast<uint32>(CenterRect.Bottom - CenterRect.Top);
        ActiveState->Viewport->Resize(NewStartX, NewStartY, NewWidth, NewHeight);

        // 본 오버레이 재구축
        if (ActiveState->bShowBones)
        {
            ActiveState->bBoneLinesDirty = true;
        }
        if (ActiveState->bShowBones && ActiveState->PreviewActor && ActiveState->CurrentMesh && ActiveState->bBoneLinesDirty)
        {
            if (ULineComponent* LineComp = ActiveState->PreviewActor->GetBoneLineComponent())
            {
                LineComp->SetLineVisible(true);
            }
            ActiveState->PreviewActor->RebuildBoneLines(ActiveState->SelectedBoneIndex);
            ActiveState->bBoneLinesDirty = false;
        }

        // 뷰포트 렌더링 (ImGui보다 먼저)
        ActiveState->Viewport->Render();
    }
}

void SSkeletalMeshViewerWindow::OpenNewTab(const char* Name)
{
    ViewerState* State = SkeletalViewerBootstrap::CreateViewerState(Name, World, Device);
    if (!State) return;

    Tabs.Add(State);
    ActiveTabIndex = Tabs.Num() - 1;
    ActiveState = State;
}

void SSkeletalMeshViewerWindow::CloseTab(int Index)
{
    if (Index < 0 || Index >= Tabs.Num()) return;
    ViewerState* State = Tabs[Index];
    SkeletalViewerBootstrap::DestroyViewerState(State);
    Tabs.RemoveAt(Index);
    if (Tabs.Num() == 0) { ActiveTabIndex = -1; ActiveState = nullptr; }
    else { ActiveTabIndex = std::min(Index, Tabs.Num() - 1); ActiveState = Tabs[ActiveTabIndex]; }
}

void SSkeletalMeshViewerWindow::LoadSkeletalMesh(const FString& Path)
{
    if (!ActiveState || Path.empty())
        return;

    // Load the skeletal mesh using the resource manager
    USkeletalMesh* Mesh = UResourceManager::GetInstance().Load<USkeletalMesh>(Path);
    if (Mesh && ActiveState->PreviewActor)
    {
        // Set the mesh on the preview actor
        ActiveState->PreviewActor->SetSkeletalMesh(Path);
        ActiveState->CurrentMesh = Mesh;
        ActiveState->LoadedMeshPath = Path;  // Track for resource unloading

        // Update mesh path buffer for display in UI
        strncpy_s(ActiveState->MeshPathBuffer, Path.c_str(), sizeof(ActiveState->MeshPathBuffer) - 1);

        // Sync mesh visibility with checkbox state
        if (auto* Skeletal = ActiveState->PreviewActor->GetSkeletalMeshComponent())
        {
            Skeletal->SetVisibility(ActiveState->bShowMesh);
        }

        // Mark bone lines as dirty to rebuild on next frame
        ActiveState->bBoneLinesDirty = true;

        // Clear and sync bone line visibility
        if (auto* LineComp = ActiveState->PreviewActor->GetBoneLineComponent())
        {
            LineComp->ClearLines();
            LineComp->SetLineVisible(ActiveState->bShowBones);
        }

        UE_LOG("SSkeletalMeshViewerWindow: Loaded skeletal mesh from %s", Path.c_str());
    }
    else
    {
        UE_LOG("SSkeletalMeshViewerWindow: Failed to load skeletal mesh from %s", Path.c_str());
    }
}

void SSkeletalMeshViewerWindow::UpdateBoneTransformFromSkeleton(ViewerState* State)
{
    if (!State || !State->CurrentMesh || State->SelectedBoneIndex < 0)
        return;
        
    // 본의 로컬 트랜스폼에서 값 추출
    const FTransform& BoneTransform = State->PreviewActor->GetSkeletalMeshComponent()->GetBoneLocalTransform(State->SelectedBoneIndex);
    State->EditBoneLocation = BoneTransform.Translation;
    State->EditBoneRotation = BoneTransform.Rotation.ToEulerZYXDeg();
    State->EditBoneScale = BoneTransform.Scale3D;
}

void SSkeletalMeshViewerWindow::ApplyBoneTransform(ViewerState* State)
{
    if (!State || !State->CurrentMesh || State->SelectedBoneIndex < 0)
        return;

    FTransform NewTransform(State->EditBoneLocation, FQuat::MakeFromEulerZYX(State->EditBoneRotation), State->EditBoneScale);
    State->PreviewActor->GetSkeletalMeshComponent()->SetBoneLocalTransform(State->SelectedBoneIndex, NewTransform);
}

void SSkeletalMeshViewerWindow::ExpandToSelectedBone(ViewerState* State, int32 BoneIndex)
{
    if (!State || !State->CurrentMesh)
        return;
        
    const FSkeleton* Skeleton = State->CurrentMesh->GetSkeleton();
    if (!Skeleton || BoneIndex < 0 || BoneIndex >= Skeleton->Bones.size())
        return;
    
    // 선택된 본부터 루트까지 모든 부모를 펼침
    int32 CurrentIndex = BoneIndex;
    while (CurrentIndex >= 0)
    {
        State->ExpandedBoneIndices.insert(CurrentIndex);
        CurrentIndex = Skeleton->Bones[CurrentIndex].ParentIndex;
    }
}

void SSkeletalMeshViewerWindow::DrawAnimationPanel(ViewerState* State)
{
    bool bHasAnimation = !!(State->CurrentAnimation);
    
    UAnimDataModel* DataModel = nullptr;
    if (bHasAnimation)
    {
         DataModel = State->CurrentAnimation->GetDataModel();
    }

    float PlayLength = 0.0f;
    int32 FrameRate = 0;
    int32 NumberOfFrames = 0;
    int32 NumberOfKeys = 0;

    if (DataModel)
    {
        PlayLength = DataModel->GetPlayLength();
        FrameRate = DataModel->GetFrameRate();
        NumberOfFrames = DataModel->GetNumberOfFrames();
        NumberOfKeys = DataModel->GetNumberOfKeys();
    }
    
    float FrameDuration = 0.0f;
    if (bHasAnimation && NumberOfFrames > 0)
    {
        FrameDuration = PlayLength / static_cast<float>(NumberOfFrames);
    }
    else
    {
        FrameDuration = (1.0f / 30.0f); // 애니메이션 없을 시 30fps로 가정
    }

    float ControlHeight = ImGui::GetFrameHeightWithSpacing();
    
    // --- 1. 메인 타임라인 에디터 (테이블 기반) ---
    ImGui::BeginChild("TimelineEditor", ImVec2(0, -ControlHeight));

    ImGuiTableFlags TableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                 ImGuiTableFlags_BordersOuter | ImGuiTableFlags_NoSavedSettings;

    if (ImGui::BeginTable("TimelineTable", 2, TableFlags))
    {
        // --- 1.1. 테이블 컬럼 설정 ---
        ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 200.0f);
        ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHeaderLabel);

        bool bIsTimelineHovered = false;
        float FrameAtMouse = 0.0f;

        // --- 1.2. 헤더 행 (필터 + 눈금자) ---
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        
        // 헤더 - 컬럼 0: 필터
        ImGui::TableSetColumnIndex(0);
        ImGui::PushItemWidth(-1);
        static char filterText[128] = "";
        ImGui::InputTextWithHint("##Filter", "Filter...", filterText, sizeof(filterText));
        ImGui::PopItemWidth();

        // 헤더 - 컬럼 1: 눈금자 (Ruler)
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.22f, 0.24f, 1.0f));
        if (ImGui::BeginChild("Ruler", ImVec2(0, ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_NoScrollbar))
        {
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 P = ImGui::GetCursorScreenPos();
            ImVec2 Size = ImGui::GetWindowSize();

            auto FrameToPixel = [&](float Frame) { return P.x + (Frame - State->TimelineOffset) * State->TimelineScale; };
            auto PixelToFrame = [&](float Pixel) { return (Pixel - P.x) / State->TimelineScale + State->TimelineOffset; };

            ImGui::InvisibleButton("##RulerInput", Size);
            if (ImGui::IsItemHovered())
            {
                bIsTimelineHovered = true;
                FrameAtMouse = PixelToFrame(ImGui::GetIO().MousePos.x);
            }

            if (bHasAnimation)
            {
                int FrameStep = 10;
                if (State->TimelineScale < 0.5f)
                {
                    FrameStep = 50;
                    
                }
                else if (State->TimelineScale < 2.0f)
                {
                    FrameStep = 20;
                }

                float StartFrame = (float)(int)PixelToFrame(P.x) - 1.0f;
                float EndFrame = (float)(int)PixelToFrame(P.x + Size.x) + 1.0f;
                StartFrame = ImMax(StartFrame, 0.0f);

                for (float F = StartFrame; F <= EndFrame && F <= NumberOfFrames; F += 1.0f)
                {
                    int Frame = (int)F;
                    float X = FrameToPixel((float)Frame);
                    if (X < P.x || X > P.x + Size.x) continue;

                    float TickHeight = (Frame % 5 == 0) ? (Size.y * 0.5f) : (Size.y * 0.3f);
                    if (Frame % FrameStep == 0) TickHeight = Size.y * 0.7f;
                    
                    DrawList->AddLine(ImVec2(X, P.y + Size.y - TickHeight), ImVec2(X, P.y + Size.y), IM_COL32(150, 150, 150, 255));
                    
                    if (Frame % FrameStep == 0)
                    {
                        char Text[16]; snprintf(Text, 16, "%d", Frame);
                        DrawList->AddText(ImVec2(X + 2, P.y), IM_COL32_WHITE, Text);
                    }
                }
            }

            if (bHasAnimation)
            {
                float PlayheadFrame = State->CurrentAnimTime / FrameDuration;
                float PlayheadX = FrameToPixel(PlayheadFrame);
                if (PlayheadX >= P.x && PlayheadX <= P.x + Size.x)
                {
                    DrawList->AddLine(ImVec2(PlayheadX, P.y), ImVec2(PlayheadX, P.y + Size.y), IM_COL32(255, 0, 0, 255), 2.0f);
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // --- 1.3. 노티파이 트랙 행 ---
        ImGui::TableNextRow();
        
        ImGui::TableSetColumnIndex(0);
        bool bNodeVisible = ImGui::TreeNodeEx("노티파이", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf);

        ImGui::TableSetColumnIndex(1);
        float TrackHeight = ImGui::GetTextLineHeight() * 1.5f;
        ImVec2 TrackSize = ImVec2(ImGui::GetContentRegionAvail().x, TrackHeight);
        
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.19f, 0.2f, 1.0f));
        if (ImGui::BeginChild("NotifyTrack", TrackSize, false, ImGuiWindowFlags_NoScrollbar))
        {
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 P = ImGui::GetCursorScreenPos();
            ImVec2 Size = ImGui::GetWindowSize();
            
            auto FrameToPixel = [&](float Frame) { return P.x + (Frame - State->TimelineOffset) * State->TimelineScale; };
            auto PixelToFrame = [&](float Pixel) { return (Pixel - P.x) / State->TimelineScale + State->TimelineOffset; };

            ImGui::InvisibleButton("##NotifyTrackInput", Size);
            if (ImGui::IsItemHovered())
            {
                bIsTimelineHovered = true;
                FrameAtMouse = PixelToFrame(ImGui::GetIO().MousePos.x);
            }
            static float RightClickFrame = 0.0f;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                RightClickFrame = PixelToFrame(ImGui::GetIO().MousePos.x);
            }

            // Context menu to add notifies
            if (ImGui::BeginPopupContextItem("NotifyTrackContext"))
            {
                if (ImGui::BeginMenu("Add Notify"))
                {
                    if (ImGui::MenuItem("Sound Notify"))
                    {
                        if (bHasAnimation && State->CurrentAnimation)
                        {
                            float ClickFrame = RightClickFrame;
                            float TimeSec = ImClamp(ClickFrame * FrameDuration, 0.0f, PlayLength);
                            // Sound Notify 추가
                            UAnimNotify_PlaySound* NewNotify = NewObject<UAnimNotify_PlaySound>();
                            if (NewNotify)
                            {
                                // 기본 SoundNotify는 sound 없음  
                                NewNotify->Sound = nullptr;
                                State->CurrentAnimation->AddPlaySoundNotify(TimeSec, NewNotify, 0.0f); 
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }

            if (bHasAnimation)
            {
                // Draw and hit-test notifies
                const TArray<FAnimNotifyEvent>& EventsConst = State->CurrentAnimation->GetAnimNotifyEvents();
                for (int i = 0; i < EventsConst.Num(); ++i)
                {
                    const FAnimNotifyEvent& Notify = EventsConst[i];
                    float TriggerFrame = Notify.TriggerTime / FrameDuration;
                    float DurationFrames = (Notify.Duration > 0.0f) ? (Notify.Duration / FrameDuration) : 0.5f;
                    
                    float XStart = FrameToPixel(TriggerFrame);
                    float XEnd = FrameToPixel(TriggerFrame + DurationFrames);

                    if (XEnd < P.x || XStart > P.x + Size.x) continue;

                    float ViewXStart = ImMax(XStart, P.x);
                    float ViewXEnd = ImMin(XEnd, P.x + Size.x);

                    if (ViewXEnd > ViewXStart)
                    {
                        // Hover/click detection rect
                        ImVec2 RMin(ViewXStart, P.y);
                        ImVec2 RMax(ViewXEnd, P.y + Size.y);
                        bool bHover = ImGui::IsMouseHoveringRect(RMin, RMax);
                        bool bClicked = bHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                            
                        // Styling
                        ImU32 FillCol = IM_COL32(100, 100, 255, bHover ? 140 : 100);
                        ImU32 LineCol = IM_COL32(200, 200, 255, 150);
                        DrawList->AddRectFilled(
                            ImVec2(ViewXStart, P.y),
                            ImVec2(ViewXEnd, P.y + Size.y),
                            FillCol
                        );
                        DrawList->AddRect(
                            ImVec2(ViewXStart, P.y), 
                            ImVec2(ViewXEnd, P.y + Size.y), 
                            LineCol
                        );
                        
                        ImGui::PushClipRect(ImVec2(ViewXStart, P.y), ImVec2(ViewXEnd, P.y + Size.y), true);
                        // Label: use NotifyName if set, otherwise fallback based on type
                        FString Label = Notify.NotifyName.ToString();
                        if (Label.empty())
                        {
                            Label = Notify.Notify && Notify.Notify->IsA<UAnimNotify_PlaySound>() ? "PlaySound" : "Notify";
                        }
                        DrawList->AddText(ImVec2(XStart + 2, P.y + 2), IM_COL32_WHITE, Label.c_str());
                        ImGui::PopClipRect();

                        if (bClicked)
                        {
                            SelectedNotifyIndex = i;
                            ImGui::OpenPopup("NotifyEditPopup");
                        }
                    }
                }
            }

            // Edit popup for a clicked notify (change sound)
            if (ImGui::BeginPopup("NotifyEditPopup"))
            {
                if (!bHasAnimation || !State->CurrentAnimation)
                {
                    ImGui::TextDisabled("No animation.");
                }
                else if (SelectedNotifyIndex < 0 || SelectedNotifyIndex >= State->CurrentAnimation->GetAnimNotifyEvents().Num())
                {
                    ImGui::TextDisabled("No notify selected.");
                }
                else
                {
                    TArray<FAnimNotifyEvent>& Events = State->CurrentAnimation->GetAnimNotifyEvents();
                    FAnimNotifyEvent& Evt = Events[SelectedNotifyIndex];

                    if (Evt.Notify && Evt.Notify->IsA<UAnimNotify_PlaySound>())
                    {
                        UAnimNotify_PlaySound* PS = static_cast<UAnimNotify_PlaySound*>(Evt.Notify);

                        // Simple selection combo from ResourceManager
                        UResourceManager& ResMgr = UResourceManager::GetInstance();
                        TArray<FString> Paths = ResMgr.GetAllFilePaths<USound>();

                        FString CurrentPath = (PS->Sound) ? PS->Sound->GetFilePath() : "None";
                        int CurrentIndex = 0; // 0 = None
                        for (int idx = 0; idx < Paths.Num(); ++idx)
                        {
                            if (Paths[idx] == CurrentPath) { CurrentIndex = idx + 1; break; }
                        }

                        // Build items on the fly: "None" + all paths
                        FString Preview = (CurrentIndex == 0) ? FString("None") : Paths[CurrentIndex - 1];
                        if (ImGui::BeginCombo("Sound", Preview.c_str()))
                        {
                            // None option
                            bool selNone = (CurrentIndex == 0);
                            if (ImGui::Selectable("None", selNone))
                            {
                                PS->Sound = nullptr;
                                Evt.NotifyName = FName("PlaySound");
                            }
                            if (selNone) ImGui::SetItemDefaultFocus();

                            for (int i = 0; i < Paths.Num(); ++i)
                            {
                                bool selected = (CurrentIndex == i + 1);
                                const FString& Item = Paths[i];
                                if (ImGui::Selectable(Item.c_str(), selected))
                                {
                                    USound* NewSound = ResMgr.Load<USound>(Item);
                                    PS->Sound = NewSound;
                                    // Set label as filename
                                    std::filesystem::path p(Item);
                                    FString Base = p.filename().string();
                                    Evt.NotifyName = FName((FString("PlaySound: ") + Base).c_str());
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        // Also allow loading from file system directly
                        if (ImGui::Button("Load .wav..."))
                        {
                            std::filesystem::path Sel = FPlatformProcess::OpenLoadFileDialog(UTF8ToWide(GDataDir) + L"/Audio", L"wav", L"WAV Files");
                            if (!Sel.empty())
                            {
                                FString PathUtf8 = WideToUTF8(Sel.generic_wstring());
                                USound* NewSound = UResourceManager::GetInstance().Load<USound>(PathUtf8);
                                if (NewSound)
                                {
                                    PS->Sound = NewSound;
                                    std::filesystem::path p2(PathUtf8);
                                    FString Base = p2.filename().string();
                                    Evt.NotifyName = FName((FString("PlaySound: ") + Base).c_str());
                                }
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("This notify type is not editable.");
                    }
                }

                ImGui::EndPopup();
            }

            if (bHasAnimation)
            {
                float PlayheadFrame = State->CurrentAnimTime / FrameDuration;
                float PlayheadX = FrameToPixel(PlayheadFrame);
                if (PlayheadX >= P.x && PlayheadX <= P.x + Size.x)
                {
                    DrawList->AddLine(ImVec2(PlayheadX, P.y), ImVec2(PlayheadX, P.y + Size.y), IM_COL32(255, 0, 0, 255), 2.0f);
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        if (bNodeVisible)
        {
            ImGui::TreePop();
        }

        // --- 1.4. 타임라인 패닝, 줌, 스크러빙 (테이블 내에서 처리) ---
        if (bHasAnimation && bIsTimelineHovered)
        {
            if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0)
            {
                float NewScale = State->TimelineScale * powf(1.1f, ImGui::GetIO().MouseWheel);
                State->TimelineScale = ImClamp(NewScale, 0.1f, 100.0f);
                State->TimelineOffset = FrameAtMouse - (ImGui::GetIO().MousePos.x - ImGui::GetCursorScreenPos().x) / State->TimelineScale;
            }
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                float DeltaFrames = ImGui::GetIO().MouseDelta.x / State->TimelineScale;
                State->TimelineOffset -= DeltaFrames;
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                State->CurrentAnimTime = ImClamp(FrameAtMouse * FrameDuration, 0.0f, PlayLength);
                State->bIsPlaying = false;
                State->bTimeChanged = true;
            }
        }

        State->TimelineOffset = std::max(State->TimelineOffset, 0.0f);

        ImGui::EndTable();
    }
    ImGui::EndChild(); // "TimelineEditor"

    ImGui::Separator();
    
    // --- 2. 하단 컨트롤 바 ---
    ImGui::BeginChild("BottomControls", ImVec2(0, ControlHeight), false, ImGuiWindowFlags_NoScrollbar);
    {
        const ImVec2 IconSizeVec(IconSize, IconSize);
        
        // 1. [첫 프레임] 버튼 
        if (IconFirstFrame && IconFirstFrame->GetShaderResourceView())
        {
            if (ImGui::ImageButton("##FirstFrameBtn", (void*)IconFirstFrame->GetShaderResourceView(), IconSizeVec))
            {
                if (bHasAnimation)
                {
                    State->CurrentAnimTime = 0.0f;
                    State->bIsPlaying = false;
                }
            }
        }
        
        ImGui::SameLine(); 

        // 2. [이전 프레임] 버튼
        if (IconPrevFrame && IconPrevFrame->GetShaderResourceView())
        {
            if (ImGui::ImageButton("##PrevFrameBtn", (void*)IconPrevFrame->GetShaderResourceView(), IconSizeVec))
            {
                if (bHasAnimation)
                {
                    State->CurrentAnimTime = ImMax(0.0f, State->CurrentAnimTime - FrameDuration); State->bIsPlaying = false;
                    State->bTimeChanged = true;
                }
            } 
        }
        ImGui::SameLine();
        
        // 3. [역재생/일시정지] 버튼
        UTexture* CurrentReverseIcon = State->bIsPlayingReverse ? IconPause : IconReversePlay;
        if (CurrentReverseIcon && CurrentReverseIcon->GetShaderResourceView())
        {
            bool bIsPlayingReverse = State->bIsPlayingReverse;
            if (bIsPlayingReverse)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            }
        
            if (ImGui::ImageButton("##ReversePlayBtn", (void*)CurrentReverseIcon->GetShaderResourceView(), IconSizeVec))
            {
                if (bIsPlayingReverse) 
                {
                    State->bIsPlaying = false;
                    State->bIsPlayingReverse = false;
                }
                else 
                {
                    State->bIsPlaying = false;
                    State->bIsPlayingReverse = true;
                }
            }
            if (bIsPlayingReverse)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::SameLine();

        // 4. [녹화] 버튼
        UTexture* CurrentRecordIcon = State->bIsRecording ? IconRecordActive : IconRecord;
        if (CurrentRecordIcon && CurrentRecordIcon->GetShaderResourceView())
        {
            bool bWasRecording = State->bIsRecording;

            if (bWasRecording)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            }

            if (ImGui::ImageButton("##RecordBtn", (void*)CurrentRecordIcon->GetShaderResourceView(), IconSizeVec))
            {
                State->bIsRecording = !State->bIsRecording; // 상태 변경
            }

            if (bWasRecording) 
            {
                ImGui::PopStyleColor(3);
            }
        }
        ImGui::SameLine();

        // 5. [재생/일시정지] 버튼
        UTexture* CurrentPlayIcon = State->bIsPlaying ? IconPause : IconPlay;
        if (CurrentPlayIcon && CurrentPlayIcon->GetShaderResourceView())
        {
            bool bIsPlaying = State->bIsPlaying;
            if (bIsPlaying)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            }
                
            if (ImGui::ImageButton("##PlayPauseBtn", (void*)CurrentPlayIcon->GetShaderResourceView(), IconSizeVec)) 
            {
                if (bIsPlaying)
                {
                    State->bIsPlaying = false;
                    State->bIsPlayingReverse = false;
                }
                else
                {
                    State->bIsPlaying = true;
                    State->bIsPlayingReverse = false;
                }
            }

            if (bIsPlaying)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::SameLine();

        // 6. [다음 프레임] 버튼
        if (IconNextFrame && IconNextFrame->GetShaderResourceView())
        {
            if (ImGui::ImageButton("##NextFrameBtn", (void*)IconNextFrame->GetShaderResourceView(), IconSizeVec)) 
            {
                if (bHasAnimation)
                {
                    State->CurrentAnimTime = ImMin(PlayLength, State->CurrentAnimTime + FrameDuration); State->bIsPlaying = false;
                    State->bTimeChanged = true;
                }
            } 
        }
        ImGui::SameLine();

        // 7. [마지막 프레임] 버튼
        if (IconLastFrame && IconLastFrame->GetShaderResourceView())
        {
            if (ImGui::ImageButton("##LastFrameBtn", (void*)IconLastFrame->GetShaderResourceView(), IconSizeVec)) 
            { 
                if (bHasAnimation)
                {
                    State->CurrentAnimTime = PlayLength;
                    State->bIsPlaying = false;
                    State->bTimeChanged = true;
                }
            } 
        }
        ImGui::SameLine();

        // 8. [루프] 버튼
        UTexture* CurrentLoopIcon = State->bIsLooping ? IconLoop : IconNoLoop;
        if (CurrentLoopIcon && CurrentLoopIcon->GetShaderResourceView())
        {
            bool bIsLooping = State->bIsLooping; 

            if (bIsLooping) 
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            }

            if (ImGui::ImageButton("##LoopBtn", (void*)CurrentLoopIcon->GetShaderResourceView(), IconSizeVec)) 
            { 
                State->bIsLooping = !State->bIsLooping;
            }

            if (bIsLooping) 
            {
                ImGui::PopStyleColor(); 
            }
        } 
        ImGui::SameLine();
        ImGui::TextDisabled("(%.2f / %.2f)", State->CurrentAnimTime, PlayLength);
    }
    ImGui::EndChild();
}
