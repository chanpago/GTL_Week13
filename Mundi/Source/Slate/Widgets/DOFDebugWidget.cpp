#include "pch.h"
#include "DOFDebugWidget.h"
#include "ImGui/imgui.h"
#include "World.h"
#include "PlayerCameraManager.h"
#include "Camera/CamMod_DOF.h"

IMPLEMENT_CLASS(UDOFDebugWidget)

UDOFDebugWidget::UDOFDebugWidget()
    : UWidget("DOF Debug")
{
}

void UDOFDebugWidget::Initialize()
{
}

void UDOFDebugWidget::Update()
{
    // 현재 DOF Modifier에서 값 동기화
    SyncFromModifier();
}

void UDOFDebugWidget::RenderWidget()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

    // DOF Enable 체크박스
    bool bPrevEnabled = bDOFEnabled;
    if (ImGui::Checkbox("DOF Enable", &bDOFEnabled))
    {
        if (bDOFEnabled && !bPrevEnabled)
        {
            // DOF 활성화 - Modifier 생성
            ApplyDOFSettings();
        }
        else if (!bDOFEnabled && bPrevEnabled)
        {
            // DOF 비활성화 - Modifier 제거
            APlayerCameraManager* CamMgr = GetPlayerCameraManager();
            if (CamMgr)
            {
                for (int32 i = CamMgr->ActiveModifiers.Num() - 1; i >= 0; --i)
                {
                    if (UCamMod_DOF* DOF = Cast<UCamMod_DOF>(CamMgr->ActiveModifiers[i]))
                    {
                        delete DOF;
                        CamMgr->ActiveModifiers.RemoveAt(i);
                    }
                }
            }
        }
    }

    if (!bDOFEnabled)
    {
        ImGui::PopStyleVar();
        return;
    }

    ImGui::Separator();
    ImGui::Text("Focus Settings");

    bool bChanged = false;

    // Focal Distance (초점 거리)
    ImGui::Text("Focal Distance (m)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##FocalDistance", &FocalDistance, 0.1f, 0.1f, 1000.0f, "%.1f"))
    {
        bChanged = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("The distance to the focal plane in meters");
    }

    // Focal Region (선명 영역)
    ImGui::Text("Focal Region (m)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##FocalRegion", &FocalRegion, 0.1f, 0.0f, 100.0f, "%.1f"))
    {
        bChanged = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("The size of the perfectly sharp region around the focal plane");
    }

    ImGui::Separator();
    ImGui::Text("Transition Regions");

    // Near Transition Region (근경 전환 영역)
    ImGui::Text("Near Transition (m)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##NearTransition", &NearTransitionRegion, 0.1f, 0.1f, 100.0f, "%.1f"))
    {
        bChanged = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Distance over which blur transitions from sharp to max near blur");
    }

    // Far Transition Region (원경 전환 영역)
    ImGui::Text("Far Transition (m)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##FarTransition", &FarTransitionRegion, 0.1f, 0.1f, 500.0f, "%.1f"))
    {
        bChanged = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Distance over which blur transitions from sharp to max far blur");
    }

    ImGui::Separator();
    ImGui::Text("Blur Size (pixels)");

    // Max Near Blur Size
    ImGui::Text("Max Near Blur");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##MaxNearBlur", &MaxNearBlurSize, 0.5f, 0.0f, 128.0f, "%.1f"))
    {
        bChanged = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Maximum blur radius for near (foreground) objects in pixels");
    }

    // Max Far Blur Size
    ImGui::Text("Max Far Blur");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("##MaxFarBlur", &MaxFarBlurSize, 0.5f, 0.0f, 128.0f, "%.1f"))
    {
        bChanged = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Maximum blur radius for far (background) objects in pixels");
    }

    // 값이 변경되었으면 적용
    if (bChanged)
    {
        ApplyDOFSettings();
    }

    ImGui::PopStyleVar();
}

APlayerCameraManager* UDOFDebugWidget::GetPlayerCameraManager()
{
    UWorld* World = GWorld;
    if (!World)
    {
        return nullptr;
    }
    return World->GetPlayerCameraManager();
}

UCamMod_DOF* UDOFDebugWidget::FindOrCreateDOFModifier()
{
    APlayerCameraManager* CamMgr = GetPlayerCameraManager();
    if (!CamMgr)
    {
        return nullptr;
    }

    // 기존 DOF Modifier 찾기
    for (UCameraModifierBase* Mod : CamMgr->ActiveModifiers)
    {
        if (UCamMod_DOF* DOF = Cast<UCamMod_DOF>(Mod))
        {
            return DOF;
        }
    }

    // 없으면 새로 생성
    UCamMod_DOF* NewDOF = new UCamMod_DOF();
    NewDOF->bEnabled = true;
    CamMgr->ActiveModifiers.Add(NewDOF);
    return NewDOF;
}

void UDOFDebugWidget::ApplyDOFSettings()
{
    UCamMod_DOF* DOF = FindOrCreateDOFModifier();
    if (!DOF)
    {
        return;
    }

    DOF->bEnabled = bDOFEnabled;
    DOF->FocalDistance = FocalDistance;
    DOF->FocalRegion = FocalRegion;
    DOF->NearTransitionRegion = NearTransitionRegion;
    DOF->FarTransitionRegion = FarTransitionRegion;
    DOF->MaxNearBlurSize = MaxNearBlurSize;
    DOF->MaxFarBlurSize = MaxFarBlurSize;
}

void UDOFDebugWidget::SyncFromModifier()
{
    APlayerCameraManager* CamMgr = GetPlayerCameraManager();
    if (!CamMgr)
    {
        bDOFEnabled = false;
        return;
    }

    // 기존 DOF Modifier 찾기
    UCamMod_DOF* DOF = nullptr;
    for (UCameraModifierBase* Mod : CamMgr->ActiveModifiers)
    {
        if (UCamMod_DOF* Found = Cast<UCamMod_DOF>(Mod))
        {
            DOF = Found;
            break;
        }
    }

    if (DOF && DOF->bEnabled)
    {
        bDOFEnabled = true;
        FocalDistance = DOF->FocalDistance;
        FocalRegion = DOF->FocalRegion;
        NearTransitionRegion = DOF->NearTransitionRegion;
        FarTransitionRegion = DOF->FarTransitionRegion;
        MaxNearBlurSize = DOF->MaxNearBlurSize;
        MaxFarBlurSize = DOF->MaxFarBlurSize;
    }
    else
    {
        bDOFEnabled = false;
    }
}
