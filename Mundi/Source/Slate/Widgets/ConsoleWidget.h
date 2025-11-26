#pragma once
#include <mutex>
#include "Widget.h"
#include "Vector.h"
#include "ImGui/imgui.h"

/**
 * @brief Console Widget for displaying log messages and executing commands
 * Based on ImGuiConsole but restructured to fit our Widget system
 */
class UConsoleWidget : public UWidget
{
public:
	DECLARE_CLASS(UConsoleWidget, UWidget)

	void Initialize() override;
	void Update() override;
	void RenderWidget() override;

	// Console specific methods
	void AddLog(const char* fmt, ...);
	void VAddLog(const char* fmt, va_list args);
	void ClearLog();
	void ExecCommand(const char* command_line);

	// Special Member Function
	UConsoleWidget();
	~UConsoleWidget() override;

	bool IsWindowPinned() const { return bIsWindowPinned; }

private:
	// Console data
	char InputBuf[256];
	TArray<FString> Items;           // Log items
	TArray<FString> HelpCommandList;        // Available commands
	TArray<FString> History;         // Command history
	int32 HistoryPos;                // -1: new line, 0..History.Size-1 browsing history

	// UI state
	bool AutoScroll;
	bool ScrollToBottom;
	ImGuiTextFilter Filter;

	bool bIsWindowPinned;    // 콘솔 창 고정(핀) 상태
	std::mutex LogMutex;

	// Helper methods
	static int TextEditCallbackStub(ImGuiInputTextCallbackData* data);
	int TextEditCallback(ImGuiInputTextCallbackData* data);

	// String utilities
	static int Stricmp(const char* s1, const char* s2);
	static int Strnicmp(const char* s1, const char* s2, int n);
	static void Strtrim(char* s);

	// Rendering helpers
	void RenderLogOutput();
	void RenderCommandInput();
	void RenderToolbar();
};
