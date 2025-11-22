#include "pch.h"
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

IMPLEMENT_CLASS(UInputManager)

UInputManager::UInputManager()
    : WindowHandle(nullptr)
    , MousePosition(0.0f, 0.0f)
    , PreviousMousePosition(0.0f, 0.0f)
    , ScreenSize(1.0f, 1.0f)
    , MouseWheelDelta(0.0f)
{
    // 배열 초기화
    memset(MouseButtons, false, sizeof(MouseButtons));
    memset(PreviousMouseButtons, false, sizeof(PreviousMouseButtons));
    memset(KeyStates, false, sizeof(KeyStates));
    memset(PreviousKeyStates, false, sizeof(PreviousKeyStates));
}

UInputManager::~UInputManager()
{
}

UInputManager& UInputManager::GetInstance()
{
    static UInputManager* Instance = nullptr;
    if (Instance == nullptr)
    {
        Instance = NewObject<UInputManager>();
    }
    return *Instance;
}

void UInputManager::Initialize(HWND hWindow)
{
    WindowHandle = hWindow;
    
    // 현재 마우스 위치 가져오기
    POINT CursorPos;
    GetCursorPos(&CursorPos);
    ScreenToClient(WindowHandle, &CursorPos);
    
    MousePosition.X = static_cast<float>(CursorPos.x);
    MousePosition.Y = static_cast<float>(CursorPos.y);
    PreviousMousePosition = MousePosition;

    // 초기 스크린 사이즈 설정 (클라이언트 영역)
    if (WindowHandle)
    {
        RECT rc{};
        if (GetClientRect(WindowHandle, &rc))
        {
            float w = static_cast<float>(rc.right - rc.left);
            float h = static_cast<float>(rc.bottom - rc.top);
            ScreenSize.X = (w > 0.0f) ? w : 1.0f;
            ScreenSize.Y = (h > 0.0f) ? h : 1.0f;
        }
    }
}

void UInputManager::Update()
{
    // 마우스 휠 델타 초기화 (프레임마다 리셋)
    MouseWheelDelta = 0.0f;

    // 매 프레임마다 실시간 마우스 위치 업데이트
    if (WindowHandle)
    {
        POINT CursorPos;
        if (GetCursorPos(&CursorPos))
        {
            ScreenToClient(WindowHandle, &CursorPos);

            // 커서 잠금 모드: 무한 드래그 처리
            // ImGui가 마우스를 사용 중이면 커서 잠금 모드를 비활성화
            if (bIsCursorLocked && !ImGui::GetIO().WantCaptureMouse)
            {
                MousePosition.X = static_cast<float>(CursorPos.x);
                MousePosition.Y = static_cast<float>(CursorPos.y);

                POINT lockedPoint = { static_cast<int>(LockedCursorPosition.X), static_cast<int>(LockedCursorPosition.Y) };
                ClientToScreen(WindowHandle, &lockedPoint);
                SetCursorPos(lockedPoint.x, lockedPoint.y);

                PreviousMousePosition = LockedCursorPosition;
            }
            else
            {
                PreviousMousePosition = MousePosition;
                MousePosition.X = static_cast<float>(CursorPos.x);
                MousePosition.Y = static_cast<float>(CursorPos.y);
            }
        }

        // 화면 크기 갱신 (윈도우 리사이즈 대응)
        RECT Rectangle{};
        if (GetClientRect(WindowHandle, &Rectangle))
        {
            float w = static_cast<float>(Rectangle.right - Rectangle.left);
            float h = static_cast<float>(Rectangle.bottom - Rectangle.top);
            ScreenSize.X = (w > 0.0f) ? w : 1.0f;
            ScreenSize.Y = (h > 0.0f) ? h : 1.0f;
        }
    }

    memcpy(PreviousMouseButtons, MouseButtons, sizeof(MouseButtons));
    memcpy(PreviousKeyStates, KeyStates, sizeof(KeyStates));
}

void UInputManager::ProcessMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    bool IsUIHover = false;
    bool IsKeyBoardCapture = false;
    
    if (ImGui::GetCurrentContext() != nullptr)
    {
        // ImGui가 입력을 사용 중인지 확인
        ImGuiIO& io = ImGui::GetIO();
        static bool once = false;
        if (!once) { io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; once = true; }
        
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        bool bAnyItemHovered = ImGui::IsAnyItemHovered();
        // WantCaptureMouse는 마우스가 UI 위에 있거나 UI가 마우스를 사용 중일 때 true
        IsUIHover = io.WantCaptureMouse || io.WantCaptureMouseUnlessPopupClose || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        IsKeyBoardCapture = io.WantTextInput;

        // skeletal mesh 뷰어도 ImGui로 만들어져서 뷰포트에 인풋이 안먹히는 현상이 일어남. 일단은 이렇게 박아놓음.
        if (IsUIHover && !bAnyItemHovered)
        {
            #include "ImGui/imgui_internal.h"
            ImGuiContext* Ctx = ImGui::GetCurrentContext();
            ImGuiWindow* Hovered = Ctx ? Ctx->HoveredWindow : nullptr;
            if (Hovered && Hovered->Name)
            {
                const char* Name = Hovered->Name;
                if (strcmp(Name, "SkeletalMeshViewport") == 0)
                {
                    IsUIHover = false;
                }
            }
        }
        
        // 디버그 출력 (마우스 클릭 시만)
        if (bEnableDebugLogging && message == WM_LBUTTONDOWN)
        {
            char debugMsg[128];
            sprintf_s(debugMsg, "ImGui State - WantMouse: %d, WantKeyboard: %d\n", 
                      IsUIHover, IsKeyBoardCapture);
            UE_LOG(debugMsg);
        }
    }
    else
    {
        // ImGui 컨텍스트가 없으면 게임 입력을 허용
        if (bEnableDebugLogging && message == WM_LBUTTONDOWN)
        {
            UE_LOG("ImGui context not initialized - allowing game input\n");
        }
    }

    switch (message)
    {
    case WM_MOUSEMOVE:
        {
            // 항상 마우스 위치는 업데이트 (ImGui 상관없이)
            // 좌표는 16-bit signed. 반드시 GET_X/Y_LPARAM으로 부호 확장해야 함.
            int MouseX = GET_X_LPARAM(lParam);
            int MouseY = GET_Y_LPARAM(lParam);
            UpdateMousePosition(MouseX, MouseY);
        }
        break;
    case WM_SIZE:
        {
            // 클라이언트 영역 크기 업데이트
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w <= 0) w = 1;
            if (h <= 0) h = 1;
            ScreenSize.X = static_cast<float>(w);
            ScreenSize.Y = static_cast<float>(h);
        }
        break;
        
    case WM_LBUTTONDOWN:
        if (!IsUIHover)  // ImGui가 마우스를 사용하지 않을 때만
        {
            UpdateMouseButton(LeftButton, true);
            if (bEnableDebugLogging) UE_LOG("InputManager: Left Mouse Down\n");
        }
        break;
        
    case WM_LBUTTONUP:
        if (!IsUIHover)  // ImGui가 마우스를 사용하지 않을 때만
        {
            UpdateMouseButton(LeftButton, false);
            if (bEnableDebugLogging) UE_LOG("InputManager: Left Mouse UP\n");
        }
        break;
        
    case WM_RBUTTONDOWN:
        if (!IsUIHover)
        {
            UpdateMouseButton(RightButton, true);
            if (bEnableDebugLogging) UE_LOG("InputManager: Right Mouse DOWN");
        }
        else
        {
            if (bEnableDebugLogging) UE_LOG("InputManager: Right Mouse DOWN blocked by ImGui");
        }
        break;
        
    case WM_RBUTTONUP:
        if (!IsUIHover)
        {
            UpdateMouseButton(RightButton, false);
            if (bEnableDebugLogging) UE_LOG("InputManager: Right Mouse UP");
        }
        else
        {
            if (bEnableDebugLogging) UE_LOG("InputManager: Right Mouse UP blocked by ImGui");
        }
        break;
        
    case WM_MBUTTONDOWN:
        if (!IsUIHover)
        {
            UpdateMouseButton(MiddleButton, true);
        }
        break;
        
    case WM_MBUTTONUP:
        if (!IsUIHover)
        {
            UpdateMouseButton(MiddleButton, false);
        }
        break;
        
    case WM_XBUTTONDOWN:
        if (!IsUIHover)
        {
            // X버튼 구분 (X1, X2)
            WORD XButton = GET_XBUTTON_WPARAM(wParam);
            if (XButton == XBUTTON1)
                UpdateMouseButton(XButton1, true);
            else if (XButton == XBUTTON2)
                UpdateMouseButton(XButton2, true);
        }
        break;
        
    case WM_XBUTTONUP:
        if (!IsUIHover)
        {
            // X버튼 구분 (X1, X2)
            WORD XButton = GET_XBUTTON_WPARAM(wParam);
            if (XButton == XBUTTON1)
                UpdateMouseButton(XButton1, false);
            else if (XButton == XBUTTON2)
                UpdateMouseButton(XButton2, false);
        }
        break;

    case WM_MOUSEWHEEL:
        if (!IsUIHover)
        {
            // 휠 델타값 추출 (HIWORD에서 signed short로 캐스팅)
            short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            MouseWheelDelta = static_cast<float>(wheelDelta) / WHEEL_DELTA; // 정규화 (-1.0 ~ 1.0)

            if (bEnableDebugLogging)
            {
                char debugMsg[64];
                sprintf_s(debugMsg, "InputManager: Mouse Wheel - Delta: %.2f\n", MouseWheelDelta);
                UE_LOG(debugMsg);
            }
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (!IsKeyBoardCapture && !IsUIHover)  // ImGui가 키보드를 사용하지 않을 때만
        {
            // Virtual Key Code 추출
            int KeyCode = static_cast<int>(wParam);
            UpdateKeyState(KeyCode, true);
            
            // 디버그 출력
            if (bEnableDebugLogging)
            {
                char debugMsg[64];
                sprintf_s(debugMsg, "InputManager: Key Down - %d\n", KeyCode);
                UE_LOG(debugMsg);
            }
        }
        break;
        
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (!IsKeyBoardCapture)  // ImGui가 키보드를 사용하지 않을 때만
        {
            // Virtual Key Code 추출
            int KeyCode = static_cast<int>(wParam);
            UpdateKeyState(KeyCode, false);
            
            // 디버그 출력
            if (bEnableDebugLogging)
            {
                char debugMsg[64];
                sprintf_s(debugMsg, "InputManager: Key UP - %d\n", KeyCode);
                UE_LOG(debugMsg);
            }
        }
        break;
    }

}

bool UInputManager::IsMouseButtonDown(EMouseButton Button) const
{
    if (Button >= MaxMouseButtons) return false;
    return MouseButtons[Button];
}

bool UInputManager::IsMouseButtonPressed(EMouseButton Button) const
{
    if (Button >= MaxMouseButtons) return false;
    
    bool currentState = MouseButtons[Button];
    bool previousState = PreviousMouseButtons[Button];
    bool isPressed = currentState && !previousState;
    
    // 디버그 출력 추가
    if (bEnableDebugLogging && Button == LeftButton && (currentState || previousState))
    {
        char debugMsg[128];
        sprintf_s(debugMsg, "IsPressed: Current=%d, Previous=%d, Result=%d\n", 
                  currentState, previousState, isPressed);
        UE_LOG(debugMsg);
    }
    
    return isPressed;
}

bool UInputManager::IsMouseButtonReleased(EMouseButton Button) const
{
    if (Button >= MaxMouseButtons) return false;
    return !MouseButtons[Button] && PreviousMouseButtons[Button];
}

bool UInputManager::IsKeyDown(int KeyCode) const
{
    if (KeyCode < 0 || KeyCode >= 256) return false; 
    return KeyStates[KeyCode];
}

bool UInputManager::IsKeyPressed(int KeyCode) const
{
    if (KeyCode < 0 || KeyCode >= 256) return false;
    return KeyStates[KeyCode] && !PreviousKeyStates[KeyCode];
}

bool UInputManager::IsKeyReleased(int KeyCode) const
{
    if (KeyCode < 0 || KeyCode >= 256) return false;
    return !KeyStates[KeyCode] && PreviousKeyStates[KeyCode];
}

void UInputManager::UpdateMousePosition(int X, int Y)
{
    MousePosition.X = static_cast<float>(X);
    MousePosition.Y = static_cast<float>(Y);
}

void UInputManager::UpdateMouseButton(EMouseButton Button, bool bPressed)
{
    if (Button < MaxMouseButtons)
    {
        MouseButtons[Button] = bPressed;
    }
}

void UInputManager::UpdateKeyState(int KeyCode, bool bPressed)
{
    if (KeyCode >= 0 && KeyCode < 256)
    {
        KeyStates[KeyCode] = bPressed;
    }
}

FVector2D UInputManager::GetScreenSize() const
{
    // 우선 ImGui 컨텍스트가 있으면 IO의 DisplaySize 사용
    if (ImGui::GetCurrentContext() != nullptr)
    {
        const ImGuiIO& io = ImGui::GetIO();
        float w = io.DisplaySize.x;
        float h = io.DisplaySize.y;
        if (w > 1.0f && h > 1.0f)
        {
            return FVector2D(w, h);
        }
    }

    // 윈도우 클라이언트 영역 쿼리
    HWND hwnd = WindowHandle ? WindowHandle : GetActiveWindow();
    if (hwnd)
    {
        RECT rc{};
        if (GetClientRect(hwnd, &rc))
        {
            float w = static_cast<float>(rc.right - rc.left);
            float h = static_cast<float>(rc.bottom - rc.top);
            if (w <= 0.0f) w = 1.0f;
            if (h <= 0.0f) h = 1.0f;
            return FVector2D(w, h);
        }
    }
    return FVector2D(1.0f, 1.0f);
}

void UInputManager::SetCursorVisible(bool bVisible)
{
    if (bVisible)
    {
        while (ShowCursor(TRUE) < 0);
    }
    else
    {
        while (ShowCursor(FALSE) >= 0);
    }
}

void UInputManager::LockCursor()
{
    if (!WindowHandle) return;

    // 마우스 커서를 화면의 중점으로 고정
    RECT rc{};
    if (GetClientRect(WindowHandle, &rc))
    {
        const int centerX = rc.left + (rc.right - rc.left) / 2;
        const int centerY = rc.top + (rc.bottom - rc.top) / 2;

        LockedCursorPosition = FVector2D(static_cast<float>(centerX), static_cast<float>(centerY)); 
    }

    // 현재 커서 위치를 기준점으로 저장
   /* POINT currentCursor;
    if (GetCursorPos(&currentCursor))
    {
        ScreenToClient(WindowHandle, &currentCursor);
        LockedCursorPosition = FVector2D(static_cast<float>(currentCursor.x), static_cast<float>(currentCursor.y));
    }*/

    // 잠금 상태 설정
    bIsCursorLocked = true;

    // 마우스 위치 동기화 (델타 계산을 위해)
    MousePosition = LockedCursorPosition;
    PreviousMousePosition = LockedCursorPosition;
}

void UInputManager::ReleaseCursor()
{
    if (!WindowHandle) return;

    // 잠금 해제
    bIsCursorLocked = false;

    // 원래 커서 위치로 복원
    POINT lockedPoint = { static_cast<int>(LockedCursorPosition.X), static_cast<int>(LockedCursorPosition.Y) };
    ClientToScreen(WindowHandle, &lockedPoint);
    SetCursorPos(lockedPoint.x, lockedPoint.y);

    // 마우스 위치 동기화
    MousePosition = LockedCursorPosition;
    PreviousMousePosition = LockedCursorPosition;
}

void UInputManager::LockCursorToCenter()
{
    if (!WindowHandle) return;

    RECT rc{};
    if (GetClientRect(WindowHandle, &rc))
    {
        const int centerX = rc.left + (rc.right - rc.left) / 2;
        const int centerY = rc.top + (rc.bottom - rc.top) / 2;

        LockedCursorPosition = FVector2D(static_cast<float>(centerX), static_cast<float>(centerY));

        POINT screenPt{ centerX, centerY };
        ClientToScreen(WindowHandle, &screenPt);
        SetCursorPos(screenPt.x, screenPt.y);

        // 동기화 (첫 프레임 델타 0)
        MousePosition = LockedCursorPosition;
        PreviousMousePosition = LockedCursorPosition;
    }
}
