#include "pch.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi1_2.h>

#include "StatsOverlayD2D.h"
#include "UIManager.h"
#include "MemoryManager.h"
#include "Picking.h"
#include "PlatformTime.h"
#include "DecalStatManager.h"
#include "TileCullingStats.h"
#include "LightStats.h"
#include "ShadowStats.h"
#include "SkinningStats.h"

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

static inline void SafeRelease(IUnknown* p) { if (p) p->Release(); }

UStatsOverlayD2D& UStatsOverlayD2D::Get()
{
	static UStatsOverlayD2D Instance;
	return Instance;
}

void UStatsOverlayD2D::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, IDXGISwapChain* InSwapChain)
{
	D3DDevice = InDevice;
	D3DContext = InContext;
	SwapChain = InSwapChain;
	bInitialized = (D3DDevice && D3DContext && SwapChain);
}

void UStatsOverlayD2D::Shutdown()
{
	D3DDevice = nullptr;
	D3DContext = nullptr;
	SwapChain = nullptr;
	bInitialized = false;
}

void UStatsOverlayD2D::EnsureInitialized()
{
}

void UStatsOverlayD2D::ReleaseD2DTarget()
{
}

static void DrawTextBlock(
	ID2D1DeviceContext* InD2dCtx,
	IDWriteFactory* InDwrite,
	const wchar_t* InText,
	const D2D1_RECT_F& InRect,
	float InFontSize,
	D2D1::ColorF InBgColor,
	D2D1::ColorF InTextColor)
{
	if (!InD2dCtx || !InDwrite || !InText) return;

	ID2D1SolidColorBrush* BrushFill = nullptr;
	InD2dCtx->CreateSolidColorBrush(InBgColor, &BrushFill);

	ID2D1SolidColorBrush* BrushText = nullptr;
	InD2dCtx->CreateSolidColorBrush(InTextColor, &BrushText);

	InD2dCtx->FillRectangle(InRect, BrushFill);

	IDWriteTextFormat* Format = nullptr;
	InDwrite->CreateTextFormat(
		L"Segoe UI",
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		InFontSize,
		L"en-us",
		&Format);

	if (Format)
	{
		Format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
		Format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
		InD2dCtx->DrawTextW(
			InText,
			static_cast<UINT32>(wcslen(InText)),
			Format,
			InRect,
			BrushText);
		Format->Release();
	}

	SafeRelease(BrushText);
	SafeRelease(BrushFill);
}

void UStatsOverlayD2D::Draw()
{
	if (!bInitialized || (!bShowFPS && !bShowMemory && !bShowPicking && !bShowDecal && !bShowTileCulling && !bShowLights && !bShowShadow && !bShowSkinning) || !SwapChain)
		return;

	ID2D1Factory1* D2dFactory = nullptr;
	D2D1_FACTORY_OPTIONS Opts{};
#ifdef _DEBUG
	Opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
	if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &Opts, (void**)&D2dFactory)))
		return;

	IDXGISurface* Surface = nullptr;
	if (FAILED(SwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&Surface)))
	{
		SafeRelease(D2dFactory);
		return;
	}

	IDXGIDevice* DxgiDevice = nullptr;
	if (FAILED(D3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&DxgiDevice)))
	{
		SafeRelease(Surface);
		SafeRelease(D2dFactory);
		return;
	}

	ID2D1Device* D2dDevice = nullptr;
	if (FAILED(D2dFactory->CreateDevice(DxgiDevice, &D2dDevice)))
	{
		SafeRelease(DxgiDevice);
		SafeRelease(Surface);
		SafeRelease(D2dFactory);
		return;
	}

	ID2D1DeviceContext* D2dCtx = nullptr;
	if (FAILED(D2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &D2dCtx)))
	{
		SafeRelease(D2dDevice);
		SafeRelease(DxgiDevice);
		SafeRelease(Surface);
		SafeRelease(D2dFactory);
		return;
	}

	IDWriteFactory* Dwrite = nullptr;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&Dwrite)))
	{
		SafeRelease(D2dCtx);
		SafeRelease(D2dDevice);
		SafeRelease(DxgiDevice);
		SafeRelease(Surface);
		SafeRelease(D2dFactory);
		return;
	}

	D2D1_BITMAP_PROPERTIES1 BmpProps = {};
	BmpProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	BmpProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	BmpProps.dpiX = 96.0f;
	BmpProps.dpiY = 96.0f;
	BmpProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

	ID2D1Bitmap1* TargetBmp = nullptr;
	if (FAILED(D2dCtx->CreateBitmapFromDxgiSurface(Surface, &BmpProps, &TargetBmp)))
	{
		SafeRelease(Dwrite);
		SafeRelease(D2dCtx);
		SafeRelease(D2dDevice);
		SafeRelease(DxgiDevice);
		SafeRelease(Surface);
		SafeRelease(D2dFactory);
		return;
	}

	D2dCtx->SetTarget(TargetBmp);

	D2dCtx->BeginDraw();
	const float Margin = 12.0f;
	const float Space = 8.0f;   // 패널간의 간격
	const float PanelWidth = 200.0f;
	const float PanelHeight = 48.0f;
	float NextY = 70.0f;

	if (bShowFPS)
	{
		float Dt = UUIManager::GetInstance().GetDeltaTime();
		float Fps = Dt > 0.0f ? (1.0f / Dt) : 0.0f;
		float Ms = Dt * 1000.0f;

		wchar_t Buf[128];
		swprintf_s(Buf, L"FPS: %.1f\nFrame time: %.2f ms", Fps, Ms);

		D2D1_RECT_F rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + PanelHeight);
		DrawTextBlock(
			D2dCtx, Dwrite, Buf, rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::Yellow));

		NextY += PanelHeight + Space;
	}

	if (bShowPicking)
	{
		// Build the entire block in one Buffer to avoid overwriting previous lines
		wchar_t Buf[256];
		double LastMs = FWindowsPlatformTime::ToMilliseconds(CPickingSystem::GetLastPickTime());
		double TotalMs = FWindowsPlatformTime::ToMilliseconds(CPickingSystem::GetTotalPickTime());
		uint32 Count = CPickingSystem::GetPickCount();
		double AvgMs = (Count > 0) ? (TotalMs / (double)Count) : 0.0;
		swprintf_s(Buf, L"Pick Count: %u\nLast: %.3f ms\nAvg: %.3f ms\nTotal: %.3f ms", Count, LastMs, AvgMs, TotalMs);

		// Increase panel height to fit multiple lines
		const float PickPanelHeight = 96.0f;
		D2D1_RECT_F rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + PickPanelHeight);
		DrawTextBlock(
			D2dCtx, Dwrite, Buf, rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::SkyBlue));

		NextY += PickPanelHeight + Space;
	}

	if (bShowMemory)
	{
		double Mb = static_cast<double>(FMemoryManager::TotalAllocationBytes) / (1024.0 * 1024.0);

		wchar_t Buf[128];
		swprintf_s(Buf, L"Memory: %.1f MB\nAllocs: %u", Mb, FMemoryManager::TotalAllocationCount);

		D2D1_RECT_F Rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + PanelHeight);
		DrawTextBlock(
			D2dCtx, Dwrite, Buf, Rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::LightGreen));

		NextY += PanelHeight + Space;
	}

	if (bShowDecal)
	{
		// 1. FDecalStatManager로부터 통계 데이터를 가져옵니다.
		uint32_t TotalCount = FDecalStatManager::GetInstance().GetTotalDecalCount();
		//uint32_t VisibleDecalCount = FDecalStatManager::GetInstance().GetVisibleDecalCount();
		uint32_t AffectedMeshCount = FDecalStatManager::GetInstance().GetAffectedMeshCount();
		double TotalTime = FDecalStatManager::GetInstance().GetDecalPassTimeMS();
		double AverageTimePerDecal = FDecalStatManager::GetInstance().GetAverageTimePerDecalMS();
		double AverageTimePerDraw = FDecalStatManager::GetInstance().GetAverageTimePerDrawMS();

		// 2. 출력할 문자열 버퍼를 만듭니다.
		wchar_t Buf[256];
		swprintf_s(Buf, L"[Decal Stats]\nTotal: %u\nAffectedMesh: %u\n전체 소요 시간: %.3f ms\nAvg/Decal: %.3f ms\nAvg/Mesh: %.3f ms",
			TotalCount,
			AffectedMeshCount,
			TotalTime,
			AverageTimePerDecal,
			AverageTimePerDraw);

		// 3. 텍스트를 여러 줄 표시해야 하므로 패널 높이를 늘립니다.
		const float decalPanelHeight = 140.0f;
		D2D1_RECT_F rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + decalPanelHeight);

		// 4. DrawTextBlock 함수를 호출하여 화면에 그립니다. 색상은 구분을 위해 주황색(Orange)으로 설정합니다.
		DrawTextBlock(
			D2dCtx, Dwrite, Buf, rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::Orange));

		NextY += decalPanelHeight + Space;
	}

	if (bShowTileCulling)
	{
		// 1. FTileCullingStatManager로부터 통계 데이터를 가져옵니다.
		const FTileCullingStats& TileStats = FTileCullingStatManager::GetInstance().GetStats();

		// 2. 출력할 문자열 버퍼를 만듭니다.
		wchar_t Buf[512];
		swprintf_s(Buf, L"[Tile Culling Stats]\nTiles: %u x %u (%u)\nLights: %u (P:%u S:%u)\nMin/Avg/Max: %u / %.1f / %u\nCulling Eff: %.1f%%\nBuffer: %u KB",
			TileStats.TileCountX,
			TileStats.TileCountY,
			TileStats.TotalTileCount,
			TileStats.TotalLights,
			TileStats.TotalPointLights,
			TileStats.TotalSpotLights,
			TileStats.MinLightsPerTile,
			TileStats.AvgLightsPerTile,
			TileStats.MaxLightsPerTile,
			TileStats.CullingEfficiency,
			TileStats.LightIndexBufferSizeBytes / 1024);

		// 3. 텍스트를 여러 줄 표시해야 하므로 패널 높이를 늘립니다.
		const float tilePanelHeight = 160.0f;
		D2D1_RECT_F rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + tilePanelHeight);

		// 4. DrawTextBlock 함수를 호출하여 화면에 그립니다. 색상은 구분을 위해 cyan으로 설정합니다.
		DrawTextBlock(
			D2dCtx, Dwrite, Buf, rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::Cyan));

		NextY += tilePanelHeight + Space;
	}

	if (bShowLights)
	{
		// 1. FLightStatManager로부터 통계 데이터를 가져옵니다.
		const FLightStats& LightStats = FLightStatManager::GetInstance().GetStats();

		// 2. 출력할 문자열 버퍼를 만듭니다.
		wchar_t Buf[512];
		swprintf_s(Buf, L"[Light Stats]\nTotal Lights: %u\n  Point: %u\n  Spot: %u\n  Directional: %u\n  Ambient: %u",
			LightStats.TotalLights,
			LightStats.TotalPointLights,
			LightStats.TotalSpotLights,
			LightStats.TotalDirectionalLights,
			LightStats.TotalAmbientLights);

		// 3. 텍스트를 여러 줄 표시해야 하므로 패널 높이를 늘립니다.
		const float lightPanelHeight = 140.0f;
		D2D1_RECT_F rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + lightPanelHeight);

		// 4. DrawTextBlock 함수를 호출하여 화면에 그립니다. 색상은 구분을 위해 보라색(Purple)으로 설정합니다.
		DrawTextBlock(
			D2dCtx, Dwrite, Buf, rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::Violet));

		NextY += lightPanelHeight + Space;
	}

	if (bShowShadow)
	{
		// 1. FShadowStatManager로부터 통계 데이터를 가져옵니다.
		const FShadowStats& ShadowStats = FShadowStatManager::GetInstance().GetStats();

		// 2. 출력할 문자열 버퍼를 만듭니다.
		wchar_t Buf[512];
		swprintf_s(Buf, L"[Shadow Stats]\nShadow Lights: %u\n  Point: %u\n  Spot: %u\n  Directional: %u\n\nAtlas 2D: %u x %u (%.1f MB)\nAtlas Cube: %u x %u x %u (%.1f MB)\n\nTotal Memory: %.1f MB",
			ShadowStats.TotalShadowCastingLights,
			ShadowStats.ShadowCastingPointLights,
			ShadowStats.ShadowCastingSpotLights,
			ShadowStats.ShadowCastingDirectionalLights,
			ShadowStats.ShadowAtlas2DSize,
			ShadowStats.ShadowAtlas2DSize,
			ShadowStats.ShadowAtlas2DMemoryMB,
			ShadowStats.ShadowAtlasCubeSize,
			ShadowStats.ShadowAtlasCubeSize,
			ShadowStats.ShadowCubeArrayCount,
			ShadowStats.ShadowAtlasCubeMemoryMB,
			ShadowStats.TotalShadowMemoryMB);

		// 3. 텍스트를 여러 줄 표시해야 하므로 패널 높이를 늘립니다.
		const float shadowPanelHeight = 260.0f;
		D2D1_RECT_F rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + shadowPanelHeight);

		// 4. DrawTextBlock 함수를 호출하여 화면에 그립니다. 색상은 구분을 위해 한색(Magenta)으로 설정합니다.
		DrawTextBlock(
			D2dCtx, Dwrite, Buf, rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::DeepPink));

		NextY += shadowPanelHeight + Space;


		rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth, NextY + 40);

		// 4. DrawTextBlock 함수를 호출하여 화면에 그립니다. 색상은 구분을 위해 한색(Magenta)으로 설정합니다.
		DrawTextBlock(
			D2dCtx, Dwrite, FScopeCycleCounter::GetTimeProfile("ShadowMapPass").GetConstWChar_tWithKey("ShadowMapPass"), rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::DeepPink));

		NextY += 40 + Space;
	}

	if (bShowSkinning)
	{		
		// GPU 스키닝
		double GPUSkinning = GET_GPU_STAT("GPUSkinning")
		// CPU 스키닝
		double CPUSkinning = FScopeCycleCounter::GetTimeProfile("CPUSkinning").GetTime();
		double Dynamic = FScopeCycleCounter::GetTimeProfile("Dynamic").GetTime();

		const FSkinningStats& SkinningStats = FSkinningStatManager::GetInstance().GetStats();
		FWideString AllSkinningType = UTF8ToWide(SkinningStats.SkinningType);		
		wchar_t Buf[512];
		swprintf_s(
			Buf,
			L"[Skeletal Stats]\n All Skinning Type : %s\n Total Skeletals : %u\n Total Bones : %u\n Total Vertices : %u\n"
			L"[Times]\n"
			L" CPU Skinning : %.3f\n"
			L" GPU Skinning : %.3f\n"
			L" Dynamic Buffer : %.3f\n",
			AllSkinningType.c_str(),
			SkinningStats.TotalSkeletals,
			SkinningStats.TotalBones,
			SkinningStats.TotalVertices,
			CPUSkinning,
			GPUSkinning,
			Dynamic
		);

		const float SkinningPanelHeight = 180.0f;
		D2D1_RECT_F rc = D2D1::RectF(Margin, NextY, Margin + PanelWidth + 50.0f, NextY + SkinningPanelHeight);

		DrawTextBlock(
			D2dCtx, Dwrite, Buf, rc, 16.0f,
			D2D1::ColorF(0, 0, 0, 0.6f),
			D2D1::ColorF(D2D1::ColorF::DeepPink));

		NextY += SkinningPanelHeight + Space;		
	}
	
	D2dCtx->EndDraw();
	D2dCtx->SetTarget(nullptr);

	FScopeCycleCounter::TimeProfileInit();


	SafeRelease(TargetBmp);
	SafeRelease(Dwrite);
	SafeRelease(D2dCtx);
	SafeRelease(D2dDevice);
	SafeRelease(DxgiDevice);
	SafeRelease(Surface);
	SafeRelease(D2dFactory);
}

void UStatsOverlayD2D::SetShowFPS(bool b)
{
	bShowFPS = b;
}

void UStatsOverlayD2D::SetShowMemory(bool b)
{
	bShowMemory = b;
}

void UStatsOverlayD2D::SetShowPicking(bool b)
{
	bShowPicking = b;
}

void UStatsOverlayD2D::SetShowDecal(bool b)
{
	bShowDecal = b;
}

void UStatsOverlayD2D::ToggleFPS()
{
	bShowFPS = !bShowFPS;
}

void UStatsOverlayD2D::ToggleMemory()
{
	bShowMemory = !bShowMemory;
}

void UStatsOverlayD2D::TogglePicking()
{
	bShowPicking = !bShowPicking;
}

void UStatsOverlayD2D::ToggleDecal()
{
	bShowDecal = !bShowDecal;
}

void UStatsOverlayD2D::SetShowTileCulling(bool b)
{
	bShowTileCulling = b;
}

void UStatsOverlayD2D::ToggleTileCulling()
{
	bShowTileCulling = !bShowTileCulling;
}

void UStatsOverlayD2D::SetShowLights(bool b)
{
	bShowLights = b;
}

void UStatsOverlayD2D::ToggleLights()
{
	bShowLights = !bShowLights;
}

void UStatsOverlayD2D::SetShowShadow(bool b)
{
	bShowShadow = b;
}

void UStatsOverlayD2D::SetShowSkinning(bool b)
{
	bShowSkinning = b;
}

void UStatsOverlayD2D::ToggleShadow()
{
	bShowShadow = !bShowShadow;
}

void UStatsOverlayD2D::ToggleSkinning()
{
	bShowSkinning = !bShowSkinning;
}
