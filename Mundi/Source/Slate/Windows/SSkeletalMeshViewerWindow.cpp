#include "pch.h"
#include "SSkeletalMeshViewerWindow.h"
#include "FViewport.h"
#include "FViewportClient.h"
#include "Source/Runtime/Engine/SkeletalViewer/SkeletalViewerBootstrap.h"
#include "Source/Editor/PlatformProcess.h"
#include "Source/Runtime/Engine/GameFramework/SkeletalMeshActor.h"
#include "Source/Runtime/Engine/Components/SkeletalMeshComponent.h"
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
#include "Source/Runtime/Core/Misc/PathUtils.h"
#include <filesystem>
#include <unordered_set>
#include <limits>
#include <cstdlib>
#include "Source/Runtime/Engine/GameFramework/CameraActor.h"
#include "Source/Runtime/Engine/Physics/PhysicsAsset.h"
#include "Source/Runtime/Engine/Physics/BodySetup.h"
#include "Source/Runtime/Engine/Physics/PhysicalMaterial.h"
#include <cstring>
#include "RenderManager.h"

namespace
{
    using FBoneNameSet = std::unordered_set<FName>;

    FBoneNameSet GatherPhysicsAssetBones(const UPhysicsAsset* PhysicsAsset)
    {
        FBoneNameSet Result;
        if (!PhysicsAsset)
        {
            return Result;
        }

        for (UBodySetup* Body : PhysicsAsset->BodySetups)
        {
            if (!Body)
            {
                continue;
            }

            if (Body->BoneName.IsValid())
            {
                Result.insert(Body->BoneName);
            }
        }

        return Result;
    }

    bool SkeletonSupportsBones(const FSkeleton* Skeleton, const FBoneNameSet& RequiredBones)
    {
        if (!Skeleton || RequiredBones.empty())
        {
            return false;
        }

        for (const FName& BoneName : RequiredBones)
        {
            if (!BoneName.IsValid())
            {
                continue;
            }

            if (Skeleton->FindBoneIndex(BoneName) == INDEX_NONE)
            {
                return false;
            }
        }

        return true;
    }

    USkeletalMesh* FindCompatibleMesh(const FBoneNameSet& RequiredBones)
    {
        if (RequiredBones.empty())
        {
            return nullptr;
        }

        USkeletalMesh* BestMatch = nullptr;
        int32 BestScore = std::numeric_limits<int32>::max();

        TArray<USkeletalMesh*> AllMeshes = UResourceManager::GetInstance().GetAll<USkeletalMesh>();
        for (USkeletalMesh* Mesh : AllMeshes)
        {
            if (!Mesh)
            {
                continue;
            }

            const FSkeleton* Skeleton = Mesh->GetSkeleton();
            if (!SkeletonSupportsBones(Skeleton, RequiredBones))
            {
                continue;
            }

            const int32 BoneDifference = std::abs(Skeleton->GetNumBones() - static_cast<int32>(RequiredBones.size()));
            if (!BestMatch || BoneDifference < BestScore)
            {
                BestMatch = Mesh;
                BestScore = BoneDifference;
            }
        }

        return BestMatch;
    }
}
SSkeletalMeshViewerWindow::SSkeletalMeshViewerWindow()
{
    PhysicsAssetPath = std::filesystem::path(GDataDir) / "Physics";

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
    // 닫을 때, Notifies를 저장 
    SaveAllNotifiesOnClose();

    // Clean up tabs if any
    for (int i = 0; i < Tabs.Num(); ++i)
    {
        ViewerState* State = Tabs[i];
        SkeletalViewerBootstrap::DestroyViewerState(State);
    }
    Tabs.Empty();
    ActiveState = nullptr;
}

// Compose default meta path under Data for an animation
static FString MakeDefaultMetaPath(const UAnimSequenceBase* Anim)
{
    FString BaseDir = GDataDir; // e.g., "Data"
    FString FileName = "AnimNotifies.anim.json";
    if (Anim)
    {
        const FString Src = Anim->GetFilePath();
        if (!Src.empty())
        {
            // Extract file name without extension
            size_t pos = Src.find_last_of("/\\");
            FString Just = (pos == FString::npos) ? Src : Src.substr(pos + 1);
            size_t dot = Just.find_last_of('.') ;
            if (dot != FString::npos) Just = Just.substr(0, dot);
            FileName = Just + ".anim.json";
        }
    }
    return NormalizePath(BaseDir + "/" + FileName);
}

void SSkeletalMeshViewerWindow::SaveAllNotifiesOnClose()
{
    for (int i = 0; i < Tabs.Num(); ++i)
    {
        ViewerState* State = Tabs[i];
        if (!State) continue;
        if (State->CurrentAnimation)
        {
            const FString OutPath = MakeDefaultMetaPath(State->CurrentAnimation);
            State->CurrentAnimation->SaveMeta(OutPath);
        }
    }
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
        if (!bSavedOnClose)
        {
            SaveAllNotifiesOnClose();
            bSavedOnClose = true;
        }
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

			/*ImGui::SameLine();*/
            if (ImGui::Checkbox("Show Bodies", &ActiveState->bShowBodies))
            {
                if (ActiveState->PreviewActor && ActiveState->PreviewActor->GetBodyLineComponent())
                {
                    ActiveState->PreviewActor->GetBodyLineComponent()->SetLineVisible(ActiveState->bShowBodies);
                }
            }

            /*ImGui::SameLine();*/
            if (ImGui::Checkbox("Show Constraints line", &ActiveState->bShowConstraintLines))
            {
                if (ActiveState->PreviewActor && ActiveState->PreviewActor->GetConstraintLineComponent())
                {
                    ActiveState->PreviewActor->GetConstraintLineComponent()->SetLineVisible(ActiveState->bShowConstraintLines);
                }
            }

            ImGui::SameLine();
            if (ImGui::Checkbox("Show Constraints limits", &ActiveState->bShowConstraintLimits))
            {
                if (ActiveState->PreviewActor && ActiveState->PreviewActor->GetConstraintLimitLineComponent())
                {
                    ActiveState->PreviewActor->GetConstraintLimitLineComponent()->SetLineVisible(ActiveState->bShowConstraintLimits);
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

                    // Track which bone was right-clicked to open context menu
                    int RightClickedBoneIndex = -1;

                    // Tree line drawing setup
                    ImDrawList* DrawList = ImGui::GetWindowDrawList();
                    const ImU32 LineColor = IM_COL32(90, 90, 90, 255);
                    const float IndentPerLevel = ImGui::GetStyle().IndentSpacing;

                    // Structure to store node position info for line drawing
                    struct NodePosInfo
                    {
                        float CenterY;
                        float BaseX;
                        int Depth;
                        int32 ParentIndex;
                        bool bIsLastChild;
                    };
                    std::vector<NodePosInfo> NodePositions;
                    NodePositions.resize(Bones.size());

                    std::function<void(int32, int, bool)> DrawNode = [&](int32 Index, int Depth, bool bIsLastChild)
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

                        // Get position before drawing tree node for line connections
                        ImVec2 CursorPos = ImGui::GetCursorScreenPos();
                        float ItemCenterY = CursorPos.y + ImGui::GetFrameHeight() * 0.5f;
                        float BaseX = CursorPos.x;

                        // Store position info for later line drawing
                        NodePositions[Index].CenterY = ItemCenterY;
                        NodePositions[Index].BaseX = BaseX;
                        NodePositions[Index].Depth = Depth;
                        NodePositions[Index].ParentIndex = Bones[Index].ParentIndex;
                        NodePositions[Index].bIsLastChild = bIsLastChild;

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
                        }

                        if (ImGui::IsItemToggledOpen())
                        {
                            if (open)
                                ActiveState->ExpandedBoneIndices.insert(Index);
                            else
                                ActiveState->ExpandedBoneIndices.erase(Index);
                        }

                        // 본 우클릭 이벤트
                        if (ImGui::BeginPopupContextItem("BoneContextMenu"))
                        {
                            if (Index >= 0 && Index < Bones.size())
                            {
                                const char* ClickedBoneName = Bones[Index].Name.c_str();
                                UPhysicsAsset* Phys = ActiveState->CurrentPhysicsAsset;
                                if (Phys && ImGui::MenuItem("Add Body"))
                                {
                                    UBodySetup* NewBody = NewObject<UBodySetup>();
                                    if (NewBody)
                                    {
                                        NewBody->BoneName = FName(ClickedBoneName);
                                        Phys->BodySetups.Add(NewBody);
                                        Phys->BuildBodySetupIndexMap();

                                        // Set newly created body as selected so right panel shows its details
                                        ActiveState->SelectedBodySetup = NewBody;
                                        UE_LOG("Added UBodySetup for bone %s to PhysicsAsset %s", ClickedBoneName, Phys->GetName().ToString().c_str());
                                    }
                                }
                            }
                            ImGui::EndPopup();
                        }

						// 본 좌클릭 이벤트
                        if (ImGui::IsItemClicked())
                        {
                            // Clear body selection when a bone itself is clicked
                            ActiveState->SelectedBodySetup = nullptr;
                            // Clear constraint selection when body is selected
                            ActiveState->SelectedConstraintIndex = -1;

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
                        
						// ======== 바디 및 컨스트레인트 UI ===========
                        if (ActiveState->CurrentPhysicsAsset)
                        {
                            UBodySetup* MatchedBody = ActiveState->CurrentPhysicsAsset->FindBodySetup(FName(Label));
                            if (MatchedBody)
                            {
								// =========== 바디 UI ============
                                ImGui::Indent(14.0f);

                                // Make body entry selectable (unique ID per bone index)
                                char BodyLabel[256];
                                snprintf(BodyLabel, sizeof(BodyLabel), "Body: %s", MatchedBody->BoneName.ToString().c_str());

                                // Highlight when the bone is selected so selection is consistent
                                bool bBodySelected = (ActiveState->SelectedBoneIndex == Index);

                                // ==== 바디 UI 좌클릭 이벤트 ====
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.25f, 1.0f));
                                if (ImGui::Selectable(BodyLabel, bBodySelected))
                                {
                                    // When user selects a body, set SelectedBodySetup and also select corresponding bone
                                    ActiveState->SelectedBodySetup = MatchedBody;

                                    ActiveState->SelectedBodyIndex = ActiveState->CurrentPhysicsAsset->FindBodyIndex(FName(Label));


                                    // Clear constraint selection when body is selected
                                    ActiveState->SelectedConstraintIndex = -1;

                                    // When user selects a body, also select the corresponding bone and move gizmo
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
                                ImGui::PopStyleColor();

                                // ===== 바디 UI 우클릭 이벤트 =======
                                if (ImGui::BeginPopupContextItem("BodyContextMenu"))
                                {
                                    UPhysicsAsset* Phys = ActiveState->CurrentPhysicsAsset;
                                    if (Phys && ImGui::BeginMenu("Add Constraint"))
                                    {
                                        // Display all bodies as potential child bodies for constraint
                                        for (UBodySetup* ChildBody : Phys->BodySetups)
                                        {
                                            if (!ChildBody) continue;

                                            // Skip self
                                            if (ChildBody == MatchedBody) continue;

                                            FString ChildBodyName = ChildBody->BoneName.ToString();
                                            if (ImGui::MenuItem(ChildBodyName.c_str()))
                                            {
                                                // Get skeleton to calculate bone transforms
                                                const FSkeleton* Skeleton = ActiveState->CurrentMesh ? ActiveState->CurrentMesh->GetSkeleton() : nullptr;
                                                assert(Skeleton && "Skeleton must be valid when adding constraints");
                                                if (Skeleton)
                                                {
                                                    int32 ParentBoneIndex = Skeleton->FindBoneIndex(MatchedBody->BoneName.ToString());
                                                    int32 ChildBoneIndex = Skeleton->FindBoneIndex(ChildBody->BoneName.ToString());

                                                    FPhysicsConstraintSetup NewConstraint;
                                                    NewConstraint.BodyNameA = MatchedBody->BoneName;  // Parent body
                                                    NewConstraint.BodyNameB = ChildBody->BoneName;    // Child body

                                                    // Calculate LocalFrameA: child bone's transform relative to parent bone's coordinate system
                                                    if (ParentBoneIndex != INDEX_NONE && ChildBoneIndex != INDEX_NONE &&
                                                        ActiveState->PreviewActor && ActiveState->PreviewActor->GetSkeletalMeshComponent())
                                                    {
                                                        FTransform ParentWorldTransform = ActiveState->PreviewActor->GetSkeletalMeshComponent()->GetBoneWorldTransform(ParentBoneIndex);
                                                        FTransform ChildWorldTransform = ActiveState->PreviewActor->GetSkeletalMeshComponent()->GetBoneWorldTransform(ChildBoneIndex);

                                                        // physicX에서는 로컬 좌표계의 scale을 아마? 무시하므로 1,1,1로 설정
                                                        ParentWorldTransform.Scale3D = FVector(1.0f, 1.0f, 1.0f);
                                                        ChildWorldTransform.Scale3D = FVector(1.0f, 1.0f, 1.0f);

                                                        // 조인트 프레임 회전 설정
                                                        // PxD6Joint 축: X=Twist, Y=Swing1(Twist 0도 기준), Z=Swing2
                                                        // 목표: 조인트 X축=본 Z축, 조인트 Y축=본 X축 (Twist 0도 기준을 +X로)
                                                        FQuat ZToX = FQuat::FromAxisAngle(FVector(0, 1, 0), -XM_PIDIV2);
                                                        FQuat TwistRef = FQuat::FromAxisAngle(FVector(1, 0, 0), -XM_PIDIV2);
                                                        FQuat JointFrameRotation = ZToX * TwistRef;

                                                        FTransform RelativeTransform = ParentWorldTransform.GetRelativeTransform(ChildWorldTransform);
                                                        NewConstraint.LocalFrameA = FTransform(RelativeTransform.Translation, RelativeTransform.Rotation * JointFrameRotation, FVector(1, 1, 1));
                                                        NewConstraint.LocalFrameB = FTransform(FVector::Zero(), JointFrameRotation, FVector(1, 1, 1));
                                                    }
                                                    else
                                                    {
                                                        // Fallback to identity if bones not found
                                                        assert(false && "Bone indices not found for constraint setup");
                                                        NewConstraint.LocalFrameA = FTransform();
                                                        NewConstraint.LocalFrameB = FTransform();
                                                    }

                                                    NewConstraint.TwistLimitMin = -45.0f;
                                                    NewConstraint.TwistLimitMax = 45.0f;
                                                    NewConstraint.SwingLimitY = 45.0f;
                                                    NewConstraint.SwingLimitZ = 45.0f;
                                                    NewConstraint.bEnableCollision = false;

                                                    // Add to physics asset
                                                    Phys->Constraints.Add(NewConstraint);

                                                    UE_LOG("Added constraint between %s (parent) and %s (child)",
                                                        MatchedBody->BoneName.ToString().c_str(),
                                                        ChildBody->BoneName.ToString().c_str());
                                                }
                                            }
                                        }
                                        ImGui::EndMenu();
                                    }
                                    ImGui::EndPopup();
                                }

								// =========== Constraint UI ============
                                ImGui::Indent(14.0f);
                                // Display constraints connected to this body
                                UPhysicsAsset* Phys = ActiveState->CurrentPhysicsAsset;
                                FName CurrentBodyName = MatchedBody->BoneName;

                                // Find all constraints involving this body
                                for (int32 ConstraintIdx = 0; ConstraintIdx < Phys->Constraints.Num(); ++ConstraintIdx)
                                {
                                    const FPhysicsConstraintSetup& Constraint = Phys->Constraints[ConstraintIdx];

                                    // Check if this constraint involves the current body
                                    if (Constraint.BodyNameA == CurrentBodyName || Constraint.BodyNameB == CurrentBodyName)
                                    {
                                        // Determine the "other" body name
                                        FName OtherBodyName = (Constraint.BodyNameA == CurrentBodyName) ? Constraint.BodyNameB : Constraint.BodyNameA;

                                        char ConstraintLabel[256];
                                        snprintf(ConstraintLabel, sizeof(ConstraintLabel), "  %s <-> %s", CurrentBodyName.ToString().c_str(), OtherBodyName.ToString().c_str());

                                        bool bConstraintSelected = (ActiveState->SelectedConstraintIndex == ConstraintIdx);

                                        // ======= Constraint UI 좌클릭 이벤트 =======
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 0.85f, 1.0f));
                                        if (ImGui::Selectable(ConstraintLabel, bConstraintSelected))
                                        {
                                            // Select this constraint
                                            ActiveState->SelectedConstraintIndex = ConstraintIdx;

											ActiveState->SelectedBoneIndex = -1; // Clear bone selection when constraint is selected

                                            // Clear body selection when constraint is selected
                                            ActiveState->SelectedBodySetup = nullptr;
                                            ActiveState->SelectedBodyIndex = -1;
                                        }
                                        ImGui::PopStyleColor();

                                        // ======= Constraint UI 우클릭 이벤트 =======
                                        if (ImGui::BeginPopupContextItem())
                                        {
                                            if (ImGui::MenuItem("Delete Constraint"))
                                            {
                                                Phys->Constraints.RemoveAt(ConstraintIdx);
                                                if (ActiveState->SelectedConstraintIndex == ConstraintIdx)
                                                {
                                                    ActiveState->SelectedConstraintIndex = -1;
                                                }
                                                else if (ActiveState->SelectedConstraintIndex > ConstraintIdx)
                                                {
                                                    ActiveState->SelectedConstraintIndex--;
                                                }
                                            }
                                            ImGui::EndPopup();
                                        }
                                    }
                                }
                                ImGui::Unindent(14.0f);

                                ImGui::Unindent(14.0f);
                            }
                        }

                        if (!bLeaf && open)
                        {
                            int32 ChildCount = Children[Index].Num();
                            for (int32 ci = 0; ci < ChildCount; ++ci)
                            {
                                int32 Child = Children[Index][ci];
                                bool bChildIsLast = (ci == ChildCount - 1);
                                DrawNode(Child, Depth + 1, bChildIsLast);
                            }

                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    };

                    // First pass: draw all tree nodes and collect positions
                    for (int32 i = 0; i < Bones.size(); ++i)
                    {
                        if (Bones[i].ParentIndex < 0)
                        {
                            DrawNode(i, 0, true);
                        }
                    }

                    // Second pass: draw connecting lines based on collected positions
                    for (int32 i = 0; i < Bones.size(); ++i)
                    {
                        const NodePosInfo& Info = NodePositions[i];
                        if (Info.Depth == 0) continue; // Skip root nodes

                        int32 ParentIdx = Info.ParentIndex;
                        if (ParentIdx < 0 || ParentIdx >= Bones.size()) continue;

                        const NodePosInfo& ParentInfo = NodePositions[ParentIdx];

                        // Calculate the X position for vertical line (at parent's indent level)
                        float LineX = Info.BaseX - IndentPerLevel + 10.0f;

                        // Draw horizontal line from vertical line to this node
                        float LineEndX = Info.BaseX - 2.0f;
                        DrawList->AddLine(ImVec2(LineX, Info.CenterY), ImVec2(LineEndX, Info.CenterY), LineColor, 2.0f);

                        // Draw vertical line from parent to this node
                        // Find the first child of parent to get the starting Y
                        float VertStartY = ParentInfo.CenterY;
                        float VertEndY = Info.CenterY;

                        // Only draw vertical segment if this is the first child or we need to connect
                        if (!Children[ParentIdx].IsEmpty())
                        {
                            int32 FirstChild = Children[ParentIdx][0];
                            if (i == FirstChild)
                            {
                                // First child: draw from parent center to this node
                                DrawList->AddLine(ImVec2(LineX, VertStartY), ImVec2(LineX, VertEndY), LineColor, 2.0f);
                            }
                            else
                            {
                                // Not first child: draw from previous sibling to this node
                                // Find previous sibling
                                for (int32 ci = 1; ci < Children[ParentIdx].Num(); ++ci)
                                {
                                    if (Children[ParentIdx][ci] == i)
                                    {
                                        int32 PrevSibling = Children[ParentIdx][ci - 1];
                                        float PrevY = NodePositions[PrevSibling].CenterY;
                                        DrawList->AddLine(ImVec2(LineX, PrevY), ImVec2(LineX, VertEndY), LineColor, 2.0f);
                                        break;
                                    }
                                }
                            }
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
                if (ImGui::IsWindowHovered())
                {
                    ImGui::GetIO().WantCaptureMouse = false;
                }

                ImVec2 childPos = ImGui::GetWindowPos();
                ImVec2 childSize = ImGui::GetWindowSize();
                ImVec2 rectMin = childPos;
                ImVec2 rectMax(childPos.x + childSize.x, childPos.y + childSize.y);
                CenterRect.Left = rectMin.x; CenterRect.Top = rectMin.y; CenterRect.Right = rectMax.x; CenterRect.Bottom = rectMax.y; CenterRect.UpdateMinMax();

                URenderer* CurrentRenderer = URenderManager::GetInstance().GetRenderer();
                D3D11RHI* RHIDevice = CurrentRenderer->GetRHIDevice();

                uint32 TotalWidth = RHIDevice->GetViewportWidth();
                uint32 TotalHeight = RHIDevice->GetViewportHeight();

                // UV 좌표를 정확하게 계산: 정확한 본 피킹을 위해 (자식 윈도우의 실제 콘텐츠 영역 사용)
                ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
                ImVec2 contentMax = ImGui::GetWindowContentRegionMax();

                // 실제 렌더링 영역의 UV 계산
                float actualLeft = CenterRect.Left + contentMin.x;
                float actualTop = CenterRect.Top + contentMin.y;

                ImVec2 uv0(actualLeft / TotalWidth, actualTop / TotalHeight);
                ImVec2 uv1((actualLeft + (contentMax.x - contentMin.x)) / TotalWidth,
                    (actualTop + (contentMax.y - contentMin.y)) / TotalHeight);

                ID3D11ShaderResourceView* SRV = RHIDevice->GetCurrentSourceSRV();

                // ImGui::Image 사용 이유: 뷰포트가 Imgui 메뉴를 가려버리는 경우 방지 위함.
                ImGui::Image((void*)SRV, ImVec2(contentMax.x - contentMin.x, contentMax.y - contentMin.y), uv0, uv1);
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

                if (ActiveState->SelectedBodySetup) // Body Properties
                {
                    UBodySetup* Body = ActiveState->SelectedBodySetup;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.90f, 0.40f, 1.0f));
                    ImGui::Text("> Selected Body");
                    ImGui::PopStyleColor();

                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.95f, 1.00f, 1.0f));
                    ImGui::TextWrapped("%s", Body->BoneName.ToString().c_str());
                    ImGui::PopStyleColor();

                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.55f, 0.70f, 0.8f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();

                    // Editable physics properties
                    ImGui::Text("Physics Properties:");
                    ImGui::Spacing();

                    //ImGui::PushItemWidth(-1);
                    ImGui::DragFloat("Mass", &Body->Mass, 0.1f, 0.01f, 10000.0f, "%.3f");
                    ImGui::DragFloat("Linear Damping", &Body->LinearDamping, 0.001f, 0.0f, 100.0f, "%.3f");
                    ImGui::DragFloat("Angular Damping", &Body->AngularDamping, 0.001f, 0.0f, 100.0f, "%.3f");
                    //ImGui::PopItemWidth();

                    ImGui::Spacing();
                    ImGui::Checkbox("Simulate Physics", &Body->bSimulatePhysics);
                    ImGui::Checkbox("Enable Gravity", &Body->bEnableGravity);
                    
					// ======UPhysicalMaterial editing=======
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.55f, 0.70f, 0.5f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

					ImGui::Text("Material Properties:");
                    UPhysicalMaterial* PhysMat = Body->PhysMaterial;
                    if (!PhysMat)
                    {
                        PhysMat = NewObject<UPhysicalMaterial>();
                        Body->PhysMaterial = PhysMat;
                    }
					ImGui::DragFloat("Static Friction", &PhysMat->StaticFriction, 0.01f, 0.0f, 2.0f, "%.3f");
					ImGui::DragFloat("Dynamic Friction", &PhysMat->DynamicFriction, 0.01f, 0.0f, 2.0f, "%.3f");
					ImGui::DragFloat("Restitution", &PhysMat->Restitution, 0.01f, 0.0f, 1.0f, "%.3f");
					ImGui::DragFloat("Density", &PhysMat->Density, 0.1f, 0.1f, 20.0f, "%.3f");


					// =======Aggregate Geometry editing========
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.55f, 0.70f, 0.5f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    // AggGeom counts
                    int NumSpheres = Body->AggGeom.SphereElements.Num();
                    int NumBoxes = Body->AggGeom.BoxElements.Num();
                    int NumCapsules = Body->AggGeom.CapsuleElements.Num();
                    int NumConvex = Body->AggGeom.ConvexElements.Num();

                    ImGui::Text("Aggregate Geometry:");
                    ImGui::Spacing();

                    // Editable Sphere Elements
                    if (ImGui::CollapsingHeader("Sphere Elements", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Indent(10.0f);
                        ImGui::PushID("SphereElements");
                        
                        for (int si = 0; si < NumSpheres; ++si)
                        {
                            FKSphereElem& S = Body->AggGeom.SphereElements[si];
                            ImGui::PushID(si);
                            
                            if (ImGui::TreeNodeEx((void*)(intptr_t)si, ImGuiTreeNodeFlags_DefaultOpen, "Sphere [%d]", si))
                            {
                                //ImGui::PushItemWidth(-1);
                                ImGui::DragFloat3("Center", &S.Center.X, 0.01f, -10000.0f, 10000.0f, "%.2f");
                                ImGui::DragFloat("Radius", &S.Radius, 0.01f, 0.01f, 10000.0f, "%.2f");
                                //ImGui::PopItemWidth();
                                
                                if (ImGui::Button("Remove Sphere"))
                                {
                                    Body->AggGeom.SphereElements.RemoveAt(si);
                                    ActiveState->bChangedGeomNum = true;
                                    ImGui::TreePop();
                                    ImGui::PopID();
                                    break;
                                }
                                
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        
                        if (ImGui::Button("Add Sphere"))
                        {
                            FKSphereElem NewSphere;
                            NewSphere.Center = FVector::Zero();
                            NewSphere.Radius = 0.5f;
                            Body->AggGeom.SphereElements.Add(NewSphere);
                            ActiveState->bChangedGeomNum = true;
                        }
                        
                        ImGui::PopID(); // Pop "SphereElements"
                        ImGui::Unindent(10.0f);
                    }

                    // Editable Box Elements
                    if (ImGui::CollapsingHeader("Box Elements"))
                    {
                        ImGui::Indent(10.0f);
                        ImGui::PushID("BoxElements");
                        
                        for (int bi = 0; bi < NumBoxes; ++bi)
                        {
                            FKBoxElem& B = Body->AggGeom.BoxElements[bi];
                            ImGui::PushID(bi);
                            
                            if (ImGui::TreeNodeEx((void*)(intptr_t)bi, ImGuiTreeNodeFlags_DefaultOpen, "Box [%d]", bi))
                            {
                                //ImGui::PushItemWidth(-1);
                                ImGui::DragFloat3("Center", &B.Center.X, 0.01f, -10000.0f, 10000.0f, "%.2f");
                                ImGui::DragFloat3("Extents", &B.Extents.X, 0.01f, 0.01f, 10000.0f, "%.2f");
                                

                                // Rotation as Euler angles
                                FVector EulerDeg = B.Rotation.ToEulerZYXDeg();
                                if (ImGui::DragFloat3("Rotation", &EulerDeg.X, 0.5f, -180.0f, 180.0f, "%.2f°"))
                                {
                                    B.Rotation = FQuat::MakeFromEulerZYX(EulerDeg);
                                }
                                //ImGui::PopItemWidth();
                                
                                if (ImGui::Button("Remove Box"))
                                {
                                    Body->AggGeom.BoxElements.RemoveAt(bi);
                                    ActiveState->bChangedGeomNum = true;
                                    ImGui::TreePop();
                                    ImGui::PopID();
                                    break;
                                }
                                
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        
                        if (ImGui::Button("Add Box"))
                        {
                            FKBoxElem NewBox;
                            NewBox.Center = FVector::Zero();
                            NewBox.Extents = FVector(0.5f, 0.5f, 0.5f);
                            NewBox.Rotation = FQuat::Identity();
                            Body->AggGeom.BoxElements.Add(NewBox);
                            ActiveState->bChangedGeomNum = true;
                        }
                        
                        ImGui::PopID(); // Pop "BoxElements"
                        ImGui::Unindent(10.0f);
                    }

                    // Editable Capsule (Capsule) Elements
                    if (ImGui::CollapsingHeader("Capsule Elements"))
                    {
                        ImGui::Indent(10.0f);
                        ImGui::PushID("CapsuleElements");
                        
                        for (int si = 0; si < NumCapsules; ++si)
                        {
                            FKCapsuleElem& S = Body->AggGeom.CapsuleElements[si];
                            ImGui::PushID(si);
                            
                            if (ImGui::TreeNodeEx((void*)(intptr_t)si, ImGuiTreeNodeFlags_DefaultOpen, "Capsule [%d]", si))
                            {
                                //ImGui::PushItemWidth(-1);
                                ImGui::DragFloat3("Center", &S.Center.X, 0.01f, -10000.0f, 10000.0f, "%.2f");
                                ImGui::DragFloat("Radius", &S.Radius, 0.01f, 0.01f, 10000.0f, "%.2f");
                                ImGui::DragFloat("Half Length", &S.HalfLength, 0.01f, 0.01f, 10000.0f, "%.2f");
                                
                                // Rotation as Euler angles
                                FVector EulerDeg = S.Rotation.ToEulerZYXDeg();
                                if (ImGui::DragFloat3("Rotation", &EulerDeg.X, 0.5f, -180.0f, 180.0f, "%.2f°"))
                                {
                                    S.Rotation = FQuat::MakeFromEulerZYX(EulerDeg);
                                }
                                //ImGui::PopItemWidth();
                                
                                if (ImGui::Button("Remove Capsule"))
                                {
                                    Body->AggGeom.CapsuleElements.RemoveAt(si);
                                    ActiveState->bChangedGeomNum = true;
                                    ImGui::TreePop();
                                    ImGui::PopID();
                                    break;
                                }
                                
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        
                        if (ImGui::Button("Add Capsule"))
                        {
                            FKCapsuleElem NewCapsule;
                            NewCapsule.Center = FVector::Zero();
                            NewCapsule.Radius = 0.3f;
                            NewCapsule.HalfLength = 0.5f;
                            NewCapsule.Rotation = FQuat::Identity();
                            Body->AggGeom.CapsuleElements.Add(NewCapsule);
                            ActiveState->bChangedGeomNum = true;
                        }
                        
                        ImGui::PopID(); // Pop "CapsuleElements"
                        ImGui::Unindent(10.0f);
                    }

                    // Convex Elements (read-only for now)
                    if (NumConvex > 0)
                    {
                        if (ImGui::CollapsingHeader("Convex Elements"))
                        {
                            ImGui::Indent(10.0f);
                            ImGui::TextDisabled("(Read-only: vertex editing not yet implemented)");
                            for (int ci = 0; ci < NumConvex; ++ci)
                            {
                                const auto& C = Body->AggGeom.ConvexElements[ci];
                                ImGui::Text("[%d] Vertices: %d", ci, C.Vertices.Num());
                            }
                            ImGui::Unindent(10.0f);
                        }
                    }
                }
                else if (ActiveState->SelectedConstraintIndex >= 0 && ActiveState->CurrentPhysicsAsset) // Constraint Properties
                {
                    UPhysicsAsset* Phys = ActiveState->CurrentPhysicsAsset;
                    if (ActiveState->SelectedConstraintIndex < Phys->Constraints.Num())
                    {
                        FPhysicsConstraintSetup& Constraint = Phys->Constraints[ActiveState->SelectedConstraintIndex];

                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.9f, 1.0f));
                        ImGui::Text("> Selected Constraint");
                        ImGui::PopStyleColor();

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.95f, 1.00f, 1.0f));
                        ImGui::TextWrapped("%s (parent) -> %s (child)", 
                            Constraint.BodyNameA.ToString().c_str(), 
                            Constraint.BodyNameB.ToString().c_str());
                        ImGui::PopStyleColor();

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.55f, 0.70f, 0.8f));
                        ImGui::Separator();
                        ImGui::PopStyleColor();

                        // Editable constraint properties
                        ImGui::Text("Constraint Limits:");
                        ImGui::Spacing();

                        // Convert radians to degrees for editing
                        float TwistMinDeg = RadiansToDegrees(Constraint.TwistLimitMin);
                        float TwistMaxDeg = RadiansToDegrees(Constraint.TwistLimitMax);
                        float SwingYDeg = RadiansToDegrees(Constraint.SwingLimitY);
                        float SwingZDeg = RadiansToDegrees(Constraint.SwingLimitZ);

                        ImGui::Text("Twist Limits (X-axis):");
                        if (ImGui::DragFloat("Min Twist##Twist", &TwistMinDeg, 0.5f, -180.0f, TwistMaxDeg, "%.2f°"))
                        {
                            Constraint.TwistLimitMin = DegreesToRadians(TwistMinDeg);
                        }
                        if (ImGui::DragFloat("Max Twist##Twist", &TwistMaxDeg, 0.5f, TwistMinDeg, 180.0f, "%.2f°"))
                        {
                            Constraint.TwistLimitMax = DegreesToRadians(TwistMaxDeg);
                        }

                        ImGui::Spacing();
                        ImGui::Text("Swing Limits:");
                        if (ImGui::DragFloat("Swing Y##Swing", &SwingYDeg, 0.5f, 0.0f, 180.0f, "%.2f°"))
                        {
                            Constraint.SwingLimitY = DegreesToRadians(SwingYDeg);
                        }
                        if (ImGui::DragFloat("Swing Z##Swing", &SwingZDeg, 0.5f, 0.0f, 180.0f, "%.2f°"))
                        {
                            Constraint.SwingLimitZ = DegreesToRadians(SwingZDeg);
                        }

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.55f, 0.70f, 0.5f));
                        ImGui::Separator();
                        ImGui::PopStyleColor();
                        ImGui::Spacing();

                        ImGui::Text("Collision:");
                        ImGui::Checkbox("Enable Collision", &Constraint.bEnableCollision);

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.55f, 0.70f, 0.5f));
                        ImGui::Separator();
                        ImGui::PopStyleColor();
                        ImGui::Spacing();

                        // Local Frame editing (advanced)
                        if (ImGui::CollapsingHeader("Local Frames (Advanced)"))
                        {
                            ImGui::Text("Local Frame A (Parent):");
                            ImGui::DragFloat3("Position A", &Constraint.LocalFrameA.Translation.X, 0.1f, -1000.0f, 1000.0f, "%.2f");
                            
                            FVector EulerA = Constraint.LocalFrameA.Rotation.ToEulerZYXDeg();
                            if (ImGui::DragFloat3("Rotation A", &EulerA.X, 0.5f, -180.0f, 180.0f, "%.2f°"))
                            {
                                Constraint.LocalFrameA.Rotation = FQuat::MakeFromEulerZYX(EulerA);
                            }

                            ImGui::Spacing();
                            ImGui::Text("Local Frame B (Child):");
                            ImGui::DragFloat3("Position B", &Constraint.LocalFrameB.Translation.X, 0.1f, -1000.0f, 1000.0f, "%.2f");
                            
                            FVector EulerB = Constraint.LocalFrameB.Rotation.ToEulerZYXDeg();
                            if (ImGui::DragFloat3("Rotation B", &EulerB.X, 0.5f, -180.0f, 180.0f, "%.2f°"))
                            {
                                Constraint.LocalFrameB.Rotation = FQuat::MakeFromEulerZYX(EulerB);
                            }
                        }
                    }
                }
                else if (ActiveState->SelectedBoneIndex >= 0 && ActiveState->CurrentMesh) // 선택된 본의 트랜스폼 편집 UI
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
                    ImGui::TextWrapped("Select a bone, body, or constraint from the hierarchy to edit its properties.");
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild(); // "BonePropertiesChild"

            ImGui::Separator();

            // 3. 하단: "Asset Browser"를 위한 자식 창 (남은 공간 모두 사용)
            ImGui::BeginChild("AssetBrowserChild", ImVec2(0, 0), true);
            {
                DrawAssetBrowserPanel(ActiveState);
            }
            ImGui::EndChild(); // "AssetBrowserChild"
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
        if (ActiveState->PreviewActor && ActiveState->CurrentMesh)
        {
            if (ActiveState->bShowBones && ActiveState->bBoneLinesDirty)
            {
                if (ULineComponent* LineComp = ActiveState->PreviewActor->GetBoneLineComponent())
                {
                    LineComp->SetLineVisible(true);
                }
                ActiveState->PreviewActor->RebuildBoneLines(ActiveState->SelectedBoneIndex);
                ActiveState->bBoneLinesDirty = false;
            }
            
			// Rebuild physics body lines and constraint lines if needed
            if (ActiveState->PreviewActor->GetSkeletalMeshComponent()->GetPhysicsAsset())
            {
                ActiveState->PreviewActor->RebuildBodyLines(ActiveState->bChangedGeomNum, ActiveState->SelectedBodyIndex);

                ActiveState->PreviewActor->RebuildConstraintLines(ActiveState->SelectedConstraintIndex);

                // Rebuild constraint angular limit visualization
                ActiveState->PreviewActor->RebuildConstraintLimitLines(ActiveState->SelectedConstraintIndex);
            }
        }

        

        // 뷰포트 렌더링 (ImGui보다 먼저)
        ActiveState->Viewport->Render();
    }
}

void SSkeletalMeshViewerWindow::LoadPhysicsAsset(UPhysicsAsset* PhysicsAsset)
{
    if (!ActiveState)
    {
        return;
    }

    ActiveState->CurrentPhysicsAsset = PhysicsAsset;

    if (!PhysicsAsset)
    {
        return;
    }

    PhysicsAsset->BuildRuntimeCache();

    FBoneNameSet RequiredBones = GatherPhysicsAssetBones(PhysicsAsset);

    const bool bHasCompatibleMesh = ActiveState->CurrentMesh && SkeletonSupportsBones(ActiveState->CurrentMesh->GetSkeleton(), RequiredBones);

    if (!bHasCompatibleMesh)
    {
        if (USkeletalMesh* AutoMesh = FindCompatibleMesh(RequiredBones))
        {
            const FString& MeshPath = AutoMesh->GetFilePath();
            if (!MeshPath.empty())
            {
                LoadSkeletalMesh(MeshPath);
                UE_LOG("SSkeletalMeshViewerWindow: Auto-loaded %s for PhysicsAsset %s", MeshPath.c_str(), PhysicsAsset->GetName().ToString().c_str());
            }
        }
        else if (!RequiredBones.empty())
        {
            UE_LOG("SSkeletalMeshViewerWindow: No compatible skeletal mesh found for PhysicsAsset %s", PhysicsAsset->GetName().ToString().c_str());
        }
    }

	USkeletalMeshComponent* SkeletalComp = ActiveState->PreviewActor->GetSkeletalMeshComponent();
    assert(SkeletalComp);
    if(SkeletalComp)
    {
        SkeletalComp->SetPhysicsAsset(PhysicsAsset);
	}
}

void SSkeletalMeshViewerWindow::SetPhysicsAssetSavePath(const FString& SavePath)
{
    if (!ActiveState || !ActiveState->CurrentPhysicsAsset)
    {
        return;
    }

    ActiveState->CurrentPhysicsAsset->SetFilePath(SavePath);
}

bool SSkeletalMeshViewerWindow::SavePhysicsAsset(ViewerState* State)
{
    if (!State || !State->CurrentPhysicsAsset)
    {
        return false;
    }

    FString TargetPath = State->CurrentPhysicsAsset->GetFilePath();

    if (TargetPath.empty())
    {
        FWideString Initial = UTF8ToWide(GDataDir) + L"/NewPhysicsAsset.phys";
        std::filesystem::path Selected = FPlatformProcess::OpenSaveFileDialog(Initial, L"physics", L"Physics Asset (*.phys)");
        if (Selected.empty())
        {
            return false;
        }

        TargetPath = ResolveAssetRelativePath(WideToUTF8(Selected.wstring()), GDataDir);
        State->CurrentPhysicsAsset->SetFilePath(TargetPath);
    }

    if (State->CurrentPhysicsAsset->SaveToFile(TargetPath))
    {
        UResourceManager::GetInstance().Add<UPhysicsAsset>(TargetPath, State->CurrentPhysicsAsset);
        UE_LOG("Physics asset saved: %s", TargetPath.c_str());
        return true;
    }

    UE_LOG("Failed to save physics asset: %s", TargetPath.c_str());
    return false;
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

        ActiveState->CurrentPhysicsAsset = ActiveState->PreviewActor->GetSkeletalMeshComponent()->GetPhysicsAsset();

        // Update mesh path buffer for display in UI
        strncpy_s(ActiveState->MeshPathBuffer, Path.c_str(), sizeof(ActiveState->MeshPathBuffer) - 1);

        // Sync mesh visibility with checkbox state
        if (auto* Skeletal = ActiveState->PreviewActor->GetSkeletalMeshComponent())
        {
            Skeletal->SetVisibility(ActiveState->bShowMesh);
        }

        // Mark bone lines as dirty to rebuild on next frame
        ActiveState->bBoneLinesDirty = true;

        // Expand all bones by default so bone tree is fully visible on load
        ActiveState->ExpandedBoneIndices.clear();
        if (const FSkeleton* Skeleton = Mesh->GetSkeleton())
        {
            for (int32 i = 0; i < Skeleton->Bones.size(); ++i)
            {
                ActiveState->ExpandedBoneIndices.insert(i);
            }
        }

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

        // --- 1.2. 헤더 행 (필터 + 눈금을자) ---
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

                ImGui::Separator();
                if (ImGui::MenuItem("Delete Notify"))
                {
                    if (bHasAnimation && State->CurrentAnimation)
                    {
                        const float ClickFrame = RightClickFrame;
                        const float ClickTimeSec = ImClamp(ClickFrame * FrameDuration, 0.0f, PlayLength);

                        TArray<FAnimNotifyEvent>& Events = State->CurrentAnimation->GetAnimNotifyEvents();

                        int DeleteIndex = -1;
                        float BestDist = 1e9f;
                        const float Tolerance = FMath::Max(FrameDuration * 0.5f, 0.05f);

                        for (int i = 0; i < Events.Num(); ++i)
                        {
                            const FAnimNotifyEvent& E = Events[i];
                            float Dist = 1e9f;
                            if (E.IsState())
                            {
                                const float Start = E.GetTriggerTime();
                                const float End = E.GetEndTriggerTime();
                                if (ClickTimeSec >= Start - Tolerance && ClickTimeSec <= End + Tolerance)
                                {
                                    Dist = 0.0f;
                                }
                                else
                                {
                                    Dist = (ClickTimeSec < Start) ? (Start - ClickTimeSec) : (ClickTimeSec - End);
                                }
                            }
                            else
                            {
                                Dist = FMath::Abs(E.GetTriggerTime() - ClickTimeSec);
                            }

                            if (Dist <= Tolerance && Dist < BestDist)
                            {
                                BestDist = Dist;
                                DeleteIndex = i;
                            }
                        }

                        if (DeleteIndex >= 0)
                        {
                            Events.RemoveAt(DeleteIndex);
                        }
                    }
                }
                ImGui::EndPopup();
            }

            if (bHasAnimation)
            {
                // Draw and hit-test notifies (now draggable)
                TArray<FAnimNotifyEvent>& Events = State->CurrentAnimation->GetAnimNotifyEvents();
                for (int i = 0; i < Events.Num(); ++i)
                {
                    FAnimNotifyEvent& Notify = Events[i];
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
                        bool bPressed = bHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                        bool bDoubleClicked = bHover && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                        

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

                        // Double-click opens edit popup; single-click starts dragging
                        if (bDoubleClicked)
                        {
                            SelectedNotifyIndex = i;
                            ImGui::OpenPopup("NotifyEditPopup");
                        }
                        else if (bPressed)
                        {
                            bDraggingNotify = true;
                            DraggingNotifyIndex = i;
                            DraggingStartMouseX = ImGui::GetIO().MousePos.x;
                            DraggingOrigTriggerTime = Notify.TriggerTime;
                            SelectedNotifyIndex = i;
                        }
                    }
                }

                // Update dragging movement (if any)
                if (bDraggingNotify && DraggingNotifyIndex >= 0 && DraggingNotifyIndex < Events.Num())
                {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        float deltaX = ImGui::GetIO().MousePos.x - DraggingStartMouseX;
                        float deltaFrames = deltaX / State->TimelineScale;
                        float newTime = DraggingOrigTriggerTime + (deltaFrames * FrameDuration);
                        newTime = ImClamp(newTime, 0.0f, PlayLength);
                        Events[DraggingNotifyIndex].TriggerTime = newTime;
                    }
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        bDraggingNotify = false;
                        DraggingNotifyIndex = -1;
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

void SSkeletalMeshViewerWindow::DrawAssetBrowserPanel(ViewerState* State)
{
    if (!State) return;

    // --- Tab Bar for switching between Animation and Physics Asset ---
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.25f, 0.35f, 0.45f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.35f, 0.50f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.30f, 0.45f, 0.60f, 1.0f));

    if (ImGui::BeginTabBar("AssetBrowserTabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Animation"))
        {
            State->AssetBrowserMode = EAssetBrowserMode::Animation;
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Physics Asset"))
        {
            State->AssetBrowserMode = EAssetBrowserMode::PhysicsAsset;
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(3);
    ImGui::Spacing();

    // Gather skeleton bone names for assets compatibility check
    TArray<FName> SkeletonBoneNames;
    const FSkeleton* SkeletonPtr = nullptr;
    if (State->CurrentMesh)
    {
        SkeletonPtr = State->CurrentMesh->GetSkeleton();
        if (SkeletonPtr)
        {
            for (const FBone& Bone : SkeletonPtr->Bones)
            {
                SkeletonBoneNames.Add(FName(Bone.Name));
            }
        }
    }

    // --- Render appropriate browser based on mode ---
    if (State->AssetBrowserMode == EAssetBrowserMode::Animation)
    {
        // --- Animation Browser Content ---
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.50f, 0.35f, 0.8f));
        ImGui::Text("Animation Assets");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.60f, 0.45f, 0.7f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        TArray<UAnimSequence*> AnimSequences = UResourceManager::GetInstance().GetAll<UAnimSequence>();

        if (SkeletonBoneNames.Num() == 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("Load a skeletal mesh first to see compatible animations.");
            ImGui::PopStyleColor();
        }
        else
        {
            TArray<UAnimSequence*> CompatibleAnims;
            for (UAnimSequence* Anim : AnimSequences)
            {
                if (!Anim) continue;
                if (Anim->IsCompatibleWith(SkeletonBoneNames))
                {
                    CompatibleAnims.Add(Anim);
                }
            }

            if (CompatibleAnims.IsEmpty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("No compatible animations found for this skeleton.");
                ImGui::PopStyleColor();
            }
            else
            {
                for (UAnimSequence* Anim : CompatibleAnims)
                {
                    if (!Anim) continue;

                    FString AssetName = Anim->GetFilePath();
                    size_t lastSlash = AssetName.find_last_of("/\\");
                    if (lastSlash != FString::npos)
                    {
                        AssetName = AssetName.substr(lastSlash + 1);
                    }

                    bool bIsSelected = (State->CurrentAnimation == Anim);

                    if (ImGui::Selectable(AssetName.c_str(), bIsSelected))
                    {
                        if (State->PreviewActor && State->PreviewActor->GetSkeletalMeshComponent())
                        {
                            State->PreviewActor->GetSkeletalMeshComponent()->SetAnimation(Anim);
                        }
                        State->CurrentAnimation = Anim;
                        State->CurrentAnimTime = 0.0f;
                        State->bIsPlaying = false;
                        State->bIsPlayingReverse = false;
                    }
                }
            }
        }
    }
    else if (State->AssetBrowserMode == EAssetBrowserMode::PhysicsAsset)
    {
        // --- Physics Asset Browser Content ---
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.50f, 0.35f, 0.25f, 0.8f));
        ImGui::Text("Physics Assets");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.60f, 0.45f, 0.35f, 0.7f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Get all Physics Assets from ResourceManager
        TArray<UPhysicsAsset*> PhysicsAssets = UResourceManager::GetInstance().GetAll<UPhysicsAsset>();

        if (SkeletonBoneNames.Num() == 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("Load a skeletal mesh first to see compatible physics assets.");
            ImGui::PopStyleColor();
        }
        else
        {
            TArray<UPhysicsAsset*> CompatiblePhysicsAssets;
            for (UPhysicsAsset* PhysAsset : PhysicsAssets)
            {
                if (!PhysAsset) continue;

				int32 TotalBodies = 0; // 전체 바디 수
				int32 MatchedBodies = 0; // 스켈레탈 메시의 본과 매칭되는 바디 수

                for (UBodySetup* Body : PhysAsset->BodySetups)
                {
                    if (!Body) continue;
                    TotalBodies++;

                    // UBodySetupCore::BoneName stores the bone mapping
                    if (SkeletonPtr && SkeletonPtr->FindBoneIndex(Body->BoneName) != INDEX_NONE)
                    {
                        MatchedBodies++;
                    }
                }

				if (TotalBodies == MatchedBodies) // 전부 일치하는 경우에만 허용
                {
                    CompatiblePhysicsAssets.Add(PhysAsset);
                }
            }

            if (CompatiblePhysicsAssets.IsEmpty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("No compatible physics assets found for this skeleton.");
                ImGui::PopStyleColor();
            }
            else
            {
                for (UPhysicsAsset* PhysAsset : CompatiblePhysicsAssets)
                {
                    if (!PhysAsset) continue;

                    FString AssetName = PhysAsset->GetName().ToString();
                    if (AssetName.empty())
                    {
                        AssetName = "Unnamed Physics Asset";
                    }

                    const FString& AssetPath = PhysAsset->GetFilePath();
                    if (!AssetPath.empty())
                    {
                        std::filesystem::path DisplayPath(AssetPath);
                        FString FileName = DisplayPath.filename().string();
                        if (!FileName.empty())
                        {
                            PhysAsset->SetName(FName(FileName.c_str()));
                            AssetName = FileName;
                        }
                    }

                    bool bIsSelected = (State->CurrentPhysicsAsset == PhysAsset);
                    bool bIsCurrentMeshPhysicsAsset = (State->CurrentMesh && State->CurrentMesh->PhysicsAsset == PhysAsset);
                    // Rename mode is per-state boolean; check it's active and refers to this asset
                    bool bIsRenamingThis = (State->bIsRenaming && bIsSelected);

                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.30f, 0.20f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.55f, 0.40f, 0.30f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.40f, 0.25f, 0.15f, 1.0f));

                    // Highlight current mesh's physics asset in blue
                    if (bIsCurrentMeshPhysicsAsset)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                    }

                    ImGui::PushID((void*)PhysAsset);
                    if (bIsRenamingThis)
                    {
                        if (State->PhysicsAssetNameBuffer[0] == '\0')
                        {
                            strncpy_s(State->PhysicsAssetNameBuffer, AssetName.c_str(), sizeof(State->PhysicsAssetNameBuffer) - 1);
                        }

                        char RenameLabel[64];
                        snprintf(RenameLabel, sizeof(RenameLabel), "##RenamePhysics_%p", (void*)PhysAsset);

                        // Request focus for the next widget (the InputText that follows)
                        ImGui::SetKeyboardFocusHere(0);
                        ImGui::PushItemWidth(-1);

                        int flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                        bool committed = ImGui::InputText(RenameLabel, State->PhysicsAssetNameBuffer, sizeof(State->PhysicsAssetNameBuffer), flags);
                        if (committed || ImGui::IsItemDeactivated() || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemActive()))
                        {
                            PhysAsset->SetName(FName(State->PhysicsAssetNameBuffer));
                            State->bIsRenaming = false;
                            State->PhysicsAssetNameBuffer[0] = '\0';
                            ImGui::ClearActiveID(); // 중요: 이전 Active ID 제거
                        }

                        ImGui::PopItemWidth();
                    }
                    else
                    {
                        if (ImGui::Selectable(AssetName.c_str(), bIsSelected))
                        {
                            State->CurrentPhysicsAsset = PhysAsset;

                            USkeletalMeshComponent* SkeletalComp = ActiveState->PreviewActor->GetSkeletalMeshComponent();
							assert(SkeletalComp);
                            if (SkeletalComp)
                            {
                                SkeletalComp->SetPhysicsAsset(PhysAsset);
                            }
                        }

                        // Enter rename mode on double-click: set per-state boolean and ensure CurrentPhysicsAsset points to the asset
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            State->CurrentPhysicsAsset = PhysAsset; // ensure target is current
                            State->bIsRenaming = true;
                            strncpy_s(State->PhysicsAssetNameBuffer, AssetName.c_str(), sizeof(State->PhysicsAssetNameBuffer) - 1);
                        }
                    }
                    ImGui::PopID();

                    // Pop the blue text color if it was applied
                    if (bIsCurrentMeshPhysicsAsset)
                    {
                        ImGui::PopStyleColor();
                    }

                    ImGui::PopStyleColor(3);

                    // Tooltip
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Physics Asset: %s", AssetName.c_str());
                        if (bIsCurrentMeshPhysicsAsset)
                        {
                            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "  (Current Mesh's Physics Asset)");
                        }
                        ImGui::EndTooltip();
                    }
                }
            }

            // New Asset button: create a new UPhysicsAsset and assign to State->CurrentPhysicsAsset
            if (ImGui::Button("New Physics Asset"))
            {
                UPhysicsAsset* NewAsset = NewObject<UPhysicsAsset>();
                if (NewAsset)
                {
                    // Give the asset a sensible default name (ObjectFactory::NewObject already set ObjectName)
                    NewAsset->SetName(NewAsset->ObjectName);

					FString AssetNameStr = NewAsset->GetName().ToString();
                    UResourceManager::GetInstance().Add<UPhysicsAsset>(AssetNameStr, NewAsset);

                    // Assign to viewer state
                    State->CurrentPhysicsAsset = NewAsset;
                    State->CurrentPhysicsAsset->SetFilePath(FString());

                    USkeletalMeshComponent* SkeletalComp = ActiveState->PreviewActor->GetSkeletalMeshComponent();
                    assert(SkeletalComp);
                    if (SkeletalComp)
                    {
                        SkeletalComp->SetPhysicsAsset(NewAsset);
                    }

                    UE_LOG("Created new PhysicsAsset: %s", NewAsset->GetName().ToString().c_str());
                }
                else
                {
                    UE_LOG("Failed to create new UPhysicsAsset");
                }
            }

            if (State->CurrentPhysicsAsset)
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.60f, 0.45f, 0.35f, 0.7f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                const FString& SavePath = State->CurrentPhysicsAsset->GetFilePath();
                if (!SavePath.empty())
                {
                    std::filesystem::path DisplayPath(SavePath);
                    FString FileName = DisplayPath.filename().string();
                    if (!FileName.empty())
                    {
                        State->CurrentPhysicsAsset->SetName(FName(FileName.c_str()));
                    }
                }

                ImGui::Text("Active Physics Asset: %s", State->CurrentPhysicsAsset->GetName().ToString().c_str());
                ImGui::Text("Save Path: %s", SavePath.empty() ? "<None>" : SavePath.c_str());

                // Save button: save currently selected PhysicsAsset to file
                if (ImGui::Button("Save to File"))
                {
                    if (State->CurrentPhysicsAsset)
                    {
                        // Get save path
                        FWideString WideInitialPath = UTF8ToWide(PhysicsAssetPath.string());
                        std::filesystem::path WidePath = FPlatformProcess::OpenSaveFileDialog(WideInitialPath, L"phys", L"Physics Asset Files");
					    FString PathStr = ResolveAssetRelativePath(WidePath.string(), PhysicsAssetPath.string());

                        if (!WidePath.empty())
                        {               
                            if (State->CurrentPhysicsAsset->SaveToFile(PathStr))
                            {
                                State->CurrentPhysicsAsset->SetFilePath(PathStr);
                            
                                UE_LOG("PhysicsAsset saved to: %s", PathStr.c_str());
                            }
                            else
                            {
                                UE_LOG("Failed to save PhysicsAsset to: %s", PathStr.c_str());
                            }
                        }
                    }
                    else
                    {
                        UE_LOG("No PhysicsAsset selected to save");
                    }
                }

				ImGui::SameLine(0.0f, 20.0f);
                if(ImGui::Button("Apply to Current Mesh"))
                {
                    if (State->CurrentMesh)
                    {
                        State->CurrentMesh->PhysicsAsset = State->CurrentPhysicsAsset;
                        UE_LOG("Applied PhysicsAsset to current SkeletalMesh");
                    }
                    else
                    {
                        UE_LOG("No SkeletalMesh loaded to apply PhysicsAsset to");
                    }
                }

                if (ImGui::Button("Generate All Body"))
                {
                    ImGui::OpenPopup("ShapeTypePopup");
                }
                auto RebuildPhysicsAssetWithShape = [&](EAggCollisionShapeType ShapeType)
                    {
                        if (!State->CurrentMesh)
                        {
                            UE_LOG("Cannot generate PhysicsAsset: No mesh available");
                            return;
                        }

                        if (!State->CurrentPhysicsAsset)
                        {
                            UE_LOG("Cannot generate PhysicsAsset: No physics asset instance available");
                            return;
                        }

                        USkeletalMeshComponent* SkeletalComponent = State->PreviewActor ? State->PreviewActor->GetSkeletalMeshComponent() : nullptr;
                        if (!SkeletalComponent)
                        {
                            UE_LOG("Cannot generate PhysicsAsset: Preview skeletal mesh component missing");
                            return;
                        }

                        State->CurrentPhysicsAsset->CreateGenerateAllBodySetup(
                            ShapeType,
                            State->CurrentMesh->GetSkeleton(),
                            SkeletalComponent
                        );

                        State->CurrentPhysicsAsset->SetFilePath(FString()); // 임시 저장 경로
                    };

                if (ImGui::BeginPopup("ShapeTypePopup"))
                {
                    if (ImGui::Selectable("Sphere"))
                    {
                        RebuildPhysicsAssetWithShape(EAggCollisionShapeType::Sphere);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Selectable("Box"))
                    {
                        RebuildPhysicsAssetWithShape(EAggCollisionShapeType::Box);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Selectable("Capsule"))
                    {
                        RebuildPhysicsAssetWithShape(EAggCollisionShapeType::Capsule);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Selectable("Convex"))
                    {
                        RebuildPhysicsAssetWithShape(EAggCollisionShapeType::Convex);
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
            }
        }
    }
}


