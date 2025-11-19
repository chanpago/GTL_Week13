#include "pch.h"
#include "ImGui/imgui_stdlib.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "K2Node_Animation.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintEvaluator.h"
#include "Source/Editor/FBX/BlendSpace/BlendSpace1D.h"

namespace ed = ax::NodeEditor;

// ----------------------------------------------------------------
//	[AnimSequence] 애니메이션 시퀀스 노드 
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_AnimSequence, UK2Node)

UK2Node_AnimSequence::UK2Node_AnimSequence()
{
    TitleColor = ImColor(100, 120, 255);
}

void UK2Node_AnimSequence::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FString AnimPath;
        if (FJsonSerializer::ReadString(InOutHandle, "AnimPath", AnimPath))
        {
            if (!AnimPath.empty())
            {
                Value = RESOURCE.Get<UAnimSequence>(AnimPath);
            }
        }
    }
    else
    {
        if (Value)
        {
            InOutHandle["AnimPath"] = Value->GetFilePath();
        }
    }
}

void UK2Node_AnimSequence::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::AnimSequence, "Value");
}

/**
 * @note ed::Suspend를 활용하지 않으면 스크린 공간이 아닌 캔버스 공간 좌표가 사용되어서
 * 정상적으로 창이 뜨지 않는 이슈 존재가 존재했었다. 현재는 해결되었다.
 */
void UK2Node_AnimSequence::RenderBody()
{
    FString PopupID = "##AnimSelectPopup_";
    PopupID += std::to_string(NodeID);

    FString PreviewName = "None";
    if (Value)
    {
        PreviewName = Value->GetFilePath();
    }

    if (ImGui::Button(PreviewName.c_str()))
    {
        ed::Suspend();
        ImGui::OpenPopup(PopupID.c_str());
        ed::Resume();
    }

    if (ImGui::IsPopupOpen(PopupID.c_str()))
    {
        ed::Suspend();
        
        if (ImGui::BeginPopup(PopupID.c_str()))
        {
            TArray<UAnimSequence*> AnimSequences = RESOURCE.GetAll<UAnimSequence>();

            for (UAnimSequence* Anim : AnimSequences)
            {
                if (!Anim) continue;

                const FString AssetName = Anim->GetFilePath();
                bool bIsSelected = (Value == Anim);
                  
                if (ImGui::Selectable(AssetName.c_str(), bIsSelected))
                {
                    Value = Anim;
                    // LoadMeta 호출 제거 - GetAnimNotifyEvents()가 이미 lazy loading을 수행함
                    // 불필요한 LoadMeta 호출은 메모리의 노티파이를 파일 데이터로 덮어씀
                    ImGui::CloseCurrentPopup();
                }

                if (bIsSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            
            ImGui::EndPopup();
        }
        ed::Resume();
    }
}

FBlueprintValue UK2Node_AnimSequence::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Value")
    {
        return Value;
    }
    return static_cast<UAnimSequence*>(nullptr);
}

// ----------------------------------------------------------------
//	[AnimStateEntry] 애니메이션 상태 머신 진입점
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_AnimStateEntry, UK2Node)

UK2Node_AnimStateEntry::UK2Node_AnimStateEntry()
{
    TitleColor = ImColor(150, 150, 150);
}

void UK2Node_AnimStateEntry::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Exec, "Entry"); 
}

void UK2Node_AnimStateEntry::RenderBody()
{
}

void UK2Node_AnimStateEntry::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

void UK2Node_AnimSequence::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[AnimState] 애니메이션 상태 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_AnimState, UK2Node)

UK2Node_AnimState::UK2Node_AnimState()
{
    TitleColor = ImColor(200, 100, 100);
}

void UK2Node_AnimState::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadString(InOutHandle, "StateName", StateName);
    }
    else
    {
        InOutHandle["StateName"] = StateName;
    }
}

void UK2Node_AnimState::AllocateDefaultPins()
{
    // 상태 머신 그래프의 흐름(Flow)을 위한 Exec 핀
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Exec, "Enter");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Exec, "Exit");

    // FAnimationState의 멤버에 해당하는 핀들
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::AnimSequence, "Animation");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "Looping", "false");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "PlayRate", "1.0");
}

void UK2Node_AnimState::RenderBody()
{
    ImGui::PushItemWidth(150.0f);
    ImGui::InputText("상태 이름", &StateName);
    ImGui::PopItemWidth();
}

void UK2Node_AnimState::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[AnimTransition] 애니메이션 전이 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_AnimTransition, UK2Node)

UK2Node_AnimTransition::UK2Node_AnimTransition()
{
    TitleColor = ImColor(100, 100, 200);
}

void UK2Node_AnimTransition::AllocateDefaultPins()
{
    // 상태 머신 그래프의 흐름(Flow)을 위한 Exec 핀
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Exec, "Execute"); 
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Exec, "Transition To");
    
    // FStateTransition의 멤버에 해당하는 핀들
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "Can Transition");
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "Blend Time");
}

void UK2Node_AnimTransition::RenderBody()
{
}

void UK2Node_AnimTransition::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[BlendSpace1D] 1D 블렌드 스페이스 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_BlendSpace1D, UK2Node)

UK2Node_BlendSpace1D::UK2Node_BlendSpace1D()
{
    TitleColor = ImColor(100, 200, 100);

    // 기본 3개 샘플 슬롯 생성
    SampleAnimations.SetNum(3);
    SamplePositions.SetNum(3);
    SamplePositions[0] = 0.0f;
    SamplePositions[1] = 100.0f;
    SamplePositions[2] = 200.0f;

    // BlendSpace 객체 생성
    BlendSpace = NewObject<UBlendSpace1D>();
}

void UK2Node_BlendSpace1D::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // 샘플 개수 로드
        int32 NumSamples = 3;
        FJsonSerializer::ReadInt32(InOutHandle, "NumSamples", NumSamples);

        SampleAnimations.SetNum(NumSamples);
        SamplePositions.SetNum(NumSamples);

        // 각 샘플 로드
        for (int32 i = 0; i < NumSamples; ++i)
        {
            FString AnimKey = "SampleAnim_" + std::to_string(i);
            FString PosKey = "SamplePos_" + std::to_string(i);

            FString AnimPath;
            if (FJsonSerializer::ReadString(InOutHandle, AnimKey, AnimPath) && !AnimPath.empty())
            {
                SampleAnimations[i] = RESOURCE.Get<UAnimSequence>(AnimPath);
            }

            FJsonSerializer::ReadFloat(InOutHandle, PosKey, SamplePositions[i]);
        }

        RebuildBlendSpace();
    }
    else
    {
        // 샘플 개수 저장
        InOutHandle["NumSamples"] = SampleAnimations.Num();

        // 각 샘플 저장
        for (int32 i = 0; i < SampleAnimations.Num(); ++i)
        {
            FString AnimKey = "SampleAnim_" + std::to_string(i);
            FString PosKey = "SamplePos_" + std::to_string(i);

            if (SampleAnimations[i])
            {
                InOutHandle[AnimKey] = SampleAnimations[i]->GetFilePath();
            }
            InOutHandle[PosKey] = SamplePositions[i];
        }
    }
}

void UK2Node_BlendSpace1D::AllocateDefaultPins()
{
    // 입력: 파라미터 (예: Speed)
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "Parameter", "0.0");

    // 출력: 블렌드된 포즈
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::AnimSequence, "Output");
}

void UK2Node_BlendSpace1D::RenderBody()
{
    // -------------------------------------------------------------------------
    // 0. 데이터 및 객체 무결성 검사
    // -------------------------------------------------------------------------
    if (!BlendSpace)
    {
        BlendSpace = NewObject<UBlendSpace1D>();
    }

    if (SamplePositions.Num() == 0 || SamplePositions.Num() != SampleAnimations.Num())
    {
        SamplePositions = { 0.0f, 100.0f };
        SampleAnimations.SetNum(2); 
        SampleAnimations[0] = nullptr; SampleAnimations[1] = nullptr;
    }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    
    // -------------------------------------------------------------------------
    // 1. Range (Min/Max) 설정 UI
    // -------------------------------------------------------------------------
    // BlendSpace 객체에서 현재 범위를 가져옵니다.
    float CurrentMin = BlendSpace->GetMinParameter();
    float CurrentMax = BlendSpace->GetMaxParameter();
    bool bRangeChanged = false;

    ImGui::PushItemWidth(60.0f); // 입력 필드 너비 고정
    
    ImGui::Text("Min"); ImGui::SameLine();
    if (ImGui::DragFloat("##MinRange", &CurrentMin, 1.0f)) bRangeChanged = true;
    
    ImGui::SameLine(); 
    ImGui::Text("Max"); ImGui::SameLine();
    if (ImGui::DragFloat("##MaxRange", &CurrentMax, 1.0f)) bRangeChanged = true;

    ImGui::PopItemWidth();

    // 값이 변경되었다면 BlendSpace에 적용하고, Max가 Min보다 작아지지 않도록 방어
    if (bRangeChanged)
    {
        if (CurrentMax < CurrentMin) CurrentMax = CurrentMin + 10.0f;
        BlendSpace->SetParameterRange(CurrentMin, CurrentMax);
    }

    // -------------------------------------------------------------------------
    // 2. 상단 컨트롤 (Add 버튼)
    // -------------------------------------------------------------------------
    float CanvasWidth = ImGui::GetContentRegionAvail().x;
    if (CanvasWidth < 200.0f) CanvasWidth = 200.0f;

    // Add 버튼 (현재 범위의 중간값에 추가)
    if (ImGui::Button("+ Add Sample", ImVec2(CanvasWidth, 0)))
    {
        float MidPos = CurrentMin + (CurrentMax - CurrentMin) * 0.5f;
        SamplePositions.Add(MidPos);
        SampleAnimations.Add(nullptr);
        RebuildBlendSpace();
    }
    
    ImGui::Dummy(ImVec2(0, 4.0f)); // 간격

    // -------------------------------------------------------------------------
    // 3. 타임라인 바 그리기
    // -------------------------------------------------------------------------
    ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    float BarHeight = 24.0f;
    
    ImVec2 BarMin = CursorPos;
    ImVec2 BarMax = ImVec2(BarMin.x + CanvasWidth, BarMin.y + BarHeight);
    
    // 바 배경 (어두운 회색)
    DrawList->AddRectFilled(BarMin, BarMax, IM_COL32(30, 30, 30, 255), 4.0f);
    DrawList->AddRect(BarMin, BarMax, IM_COL32(100, 100, 100, 255), 4.0f);

    // 공간 확보 (다음 위젯이 겹치지 않도록)
    ImGui::Dummy(ImVec2(CanvasWidth, BarHeight));

    bool bNeedRebuild = false;
    float RangeLength = CurrentMax - CurrentMin;
    if (RangeLength <= 0.0f) RangeLength = 1.0f; // 0 나누기 방지

    // -------------------------------------------------------------------------
    // 4. 샘플 포인트 루프
    // -------------------------------------------------------------------------
    for (int32 i = 0; i < SamplePositions.Num(); ++i)
    {
        ImGui::PushID(i);

        // [위치 정규화] 현재 Min/Max 범위에 맞춰 X 좌표 계산
        // 범위 밖으로 나간 샘플도 그릴 것인지, 클램핑할 것인지 결정 (여기선 클램핑)
        float ClampedPos = FMath::Clamp(SamplePositions[i], CurrentMin, CurrentMax);
        float NormalizedPos = (ClampedPos - CurrentMin) / RangeLength;
        
        float X = BarMin.x + (NormalizedPos * CanvasWidth);
        float Y_Center = BarMin.y + (BarHeight * 0.5f);

        float MarkerSize = 6.0f;
        // 마름모 좌표
        ImVec2 P1(X, BarMin.y + 2);
        ImVec2 P2(X + MarkerSize, Y_Center);
        ImVec2 P3(X, BarMax.y - 2);
        ImVec2 P4(X - MarkerSize, Y_Center);

        // ---------------------------------------------------------------------
        // 인터랙션 영역 (InvisibleButton)
        // ---------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(X - MarkerSize, BarMin.y));
        FString BtnID = "##MarkerBtn" + std::to_string(i);
        
        ImGui::InvisibleButton(BtnID.c_str(), ImVec2(MarkerSize * 2, BarHeight));
        
        bool bIsHovered = ImGui::IsItemHovered();
        bool bIsActive = ImGui::IsItemActive();

        // ---------------------------------------------------------------------
        // [드래그 로직]
        // ---------------------------------------------------------------------
        if (bIsActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            float DragDelta = ImGui::GetIO().MouseDelta.x;
            if (FMath::Abs(DragDelta) > 0.0f)
            {
                // 픽셀 -> 값 변환
                float ValueDelta = (DragDelta / CanvasWidth) * RangeLength;
                SamplePositions[i] += ValueDelta;
                
                // 현재 범위 내로 제한
                SamplePositions[i] = FMath::Clamp(SamplePositions[i], CurrentMin, CurrentMax);
                bNeedRebuild = true;
            }
        }

        // ---------------------------------------------------------------------
        // [팝업 로직] (클릭 시)
        // ---------------------------------------------------------------------
        FString PopupID = "AnimSelectPopup_" + std::to_string(NodeID) + "_" + std::to_string(i);

        if (ImGui::IsItemDeactivated())
        {
            // 드래그 거리가 짧을 때만 클릭으로 인정
            if (ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 25.0f) 
            {
                ed::Suspend(); // [좌표계 전환]
                ImGui::OpenPopup(PopupID.c_str());
                ed::Resume();
            }
        }
        
        // 우클릭 삭제
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (SamplePositions.Num() > 1)
            {
                SamplePositions.RemoveAt(i);
                SampleAnimations.RemoveAt(i);
                bNeedRebuild = true;
                ImGui::PopID();
                break; 
            }
        }

        // ---------------------------------------------------------------------
        // [팝업 렌더링]
        // ---------------------------------------------------------------------
        if (ImGui::IsPopupOpen(PopupID.c_str()))
        {
            ed::Suspend(); // [좌표계 전환]
            if (ImGui::BeginPopup(PopupID.c_str()))
            {
                ImGui::Text("Select Animation");
                ImGui::Separator();

                TArray<UAnimSequence*> AllAnims = RESOURCE.GetAll<UAnimSequence>();
                for (UAnimSequence* Anim : AllAnims)
                {
                    if (!Anim) continue;
                    bool bSelected = (SampleAnimations[i] == Anim);
                    if (ImGui::Selectable(Anim->GetFilePath().c_str(), bSelected))
                    {
                        SampleAnimations[i] = Anim;
                        bNeedRebuild = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (bSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndPopup();
            }
            ed::Resume(); // [복구]
        }

        // ---------------------------------------------------------------------
        // 시각적 피드백 (마커 & 텍스트)
        // ---------------------------------------------------------------------
        ImU32 MarkerColor = IM_COL32(200, 200, 200, 255);
        if (bIsActive) MarkerColor = IM_COL32(255, 200, 50, 255);
        else if (bIsHovered) MarkerColor = IM_COL32(255, 255, 100, 255);
        
        if (SampleAnimations[i] == nullptr) MarkerColor = IM_COL32(255, 50, 50, 255);

        DrawList->AddQuadFilled(P1, P2, P3, P4, MarkerColor);
        DrawList->AddQuad(P1, P2, P3, P4, IM_COL32(0, 0, 0, 255));

        if (SampleAnimations[i])
        {
            std::string PathStr = SampleAnimations[i]->GetFilePath().c_str();
            size_t LastSlash = PathStr.find_last_of("/\\");
            std::string FileName = (LastSlash == std::string::npos) ? PathStr : PathStr.substr(LastSlash + 1);
            if (FileName.length() > 8) FileName = FileName.substr(0, 6) + "..";

            ImVec2 TextPos = ImVec2(X - (ImGui::CalcTextSize(FileName.c_str()).x * 0.5f), BarMax.y + 2);
            DrawList->AddText(TextPos, IM_COL32(255, 255, 255, 200), FileName.c_str());
        }

        // ---------------------------------------------------------------------
        // [툴팁 로직] (위치 수정됨)
        // ---------------------------------------------------------------------
        if (bIsHovered)
        {
            ed::Suspend(); 
            ImGui::BeginTooltip();
            ImGui::Text("Index: %d", i);
            ImGui::Text("Pos: %.1f", SamplePositions[i]);
            ImGui::Text("Anim: %s", SampleAnimations[i] ? SampleAnimations[i]->GetFilePath().c_str() : "None");
            ImGui::TextDisabled("(Drag to move, Click to edit)");
            ImGui::EndTooltip();
            ed::Resume();
        }

        ImGui::PopID();
    }
    
    ImGui::Dummy(ImVec2(0, 15.0f));

    if (bNeedRebuild)
    {
        RebuildBlendSpace();
    }
}

FBlueprintValue UK2Node_BlendSpace1D::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Output")
    {
        float Parameter = FBlueprintEvaluator::EvaluateInput<float>(FindPin("Parameter"), Context);

        if (BlendSpace)
        {
            BlendSpace->SetParameter(Parameter);
        }
        return BlendSpace;
    }
    return static_cast<UBlendSpace1D*>(nullptr);
}

void UK2Node_BlendSpace1D::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

void UK2Node_BlendSpace1D::RebuildBlendSpace()
{
    if (!BlendSpace)
    {
        BlendSpace = NewObject<UBlendSpace1D>();
    }

    BlendSpace->ClearSamples();

    for (int32 i = 0; i < SampleAnimations.Num(); ++i)
    {
        if (SampleAnimations[i])
        {
            BlendSpace->AddSample(SampleAnimations[i], SamplePositions[i]);
        }
    }
}