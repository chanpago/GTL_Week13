// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UIWindow.h"
#include <filesystem>
#include <vector>

// Forward declarations
class FThumbnailManager;

/**
 * @brief 파일 엔트리 구조체
 * 컨텐츠 브라우저에서 표시할 파일/폴더 정보
 */
struct FFileEntry
{
	std::filesystem::path Path;     // 원본 경로 (std::filesystem은 윈도우에서 이미 WideChar 기반)
    
	FWideString FileNameW;          // 로직/정렬용 와이드 문자열 이름
	std::string FileNameUTF8;       // ImGui 표시용 UTF-8 이름 (매 프레임 변환 방지용 캐시)
    
	std::string Extension;          // 확장자 (소문자, UTF-8)
	bool bIsDirectory;
	uintmax_t FileSize;
};

class UContentBrowserWindow : public UUIWindow
{
	DECLARE_CLASS(UContentBrowserWindow, UUIWindow)

public:
	UContentBrowserWindow();
	virtual ~UContentBrowserWindow();

	virtual void Initialize() override;
	void RenderWindow();
	void RenderContent();

private:
	void Cleanup() override;
    
	// 경로 탐색 및 갱신
	void NavigateToPath(const std::filesystem::path& NewPath);
	void RefreshCurrentDirectory();

	// 렌더링 헬퍼
	void RenderFolderTree();
	void RenderFolderTreeNode(const std::filesystem::path& FolderPath);
	void RenderPathBar();
	void RenderContentGrid();
	void RenderFileItem(FFileEntry& Entry, int Index, bool bUseThumbnails);

	// 입력 처리
	void HandleDragSource(FFileEntry& Entry);
	void HandleDoubleClick(FFileEntry& Entry);

	// 유틸리티
	const char* GetIconForFile(const FFileEntry& Entry) const;
	FString FormatFileSize(uintmax_t Size) const;

	// 에셋 생성
	void CreateNewParticleSystem();

private:
	std::filesystem::path RootPath;
	std::filesystem::path CurrentPath;
    
	std::vector<FFileEntry> DisplayedFiles;
    
	FFileEntry* SelectedFile;
	int SelectedIndex;
    
	// 더블클릭 감지용
	double LastClickTime;
	int LastClickedIndex;

	// 레이아웃 설정값
	float ThumbnailSize;
	int ColumnsCount;
	float LeftPanelWidth;
	float SplitterWidth;
	bool bIsDraggingSplitter;

	// 표시 옵션
	bool bShowFolders;
	bool bShowFiles;
	bool bUseThumbnails;
};