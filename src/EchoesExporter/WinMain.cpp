//
// Created by miyehn on 11/21/2023.
//
// HelloWindowsDesktop.cpp
// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <tchar.h>
#include "AssetPack.h"
#include "Log.h"

// Global variables

// The main window class name.
static TCHAR szWindowClass[] = _T("EchoesExporter");
// The string that appears in the application's title bar.
static TCHAR szTitle[] = _T("Echoes Exporter (version: 11/21/23)");

// Stored instance handle for use in Win32 API calls such as FindResource
HINSTANCE hInst;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

///////////////////////////////////////////////

#define CMD_DEBUG 1
#define CMD_OPEN_PSD 2
#define CMD_PSD_FORMATTING_DOC 3
#define CMD_EXPORT_ASSETPACK 4
#define CMD_SPRITECONVERTER_DOC 5

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 720
#define WINDOW_PADDING 10
#define LOG_HEIGHT 200
#define GAP_SMALL 5
#define GAP_MEDIUM 10
#define GAP_LARGE 20

static std::string GUILog;
static HWND hLogWnd = NULL;
void AppendGUILog(const std::string& msg) {
	GUILog += msg;
	GUILog += "\r\n";
	if (!SetWindowText(hLogWnd, _T(GUILog.c_str()))) {
		ERR("Failed to update log text")
	}
	InvalidateRect(hLogWnd, NULL, true);
	UpdateWindow(hLogWnd);
}

static AssetPack assetPack;

static HWND hMaterialsList = NULL;
void AddMenus(HWND hWnd);
void AddContent(HWND hWnd);

void CmdOpenPsd() {
	EXPECT(SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)), true)

	IFileOpenDialog *pFileOpen;
	EXPECT(SUCCEEDED(CoCreateInstance(
		CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen))), true);

	if (SUCCEEDED(pFileOpen->Show(NULL))) {
		IShellItem* pItem;
		if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
			PWSTR pszFilePath;
			EXPECT(SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)), true)

			std::wstring ws(pszFilePath);
			std::string s(ws.begin(), ws.end());
			LOG("reading PSD: %s", s.c_str())

			ASSERT(hMaterialsList != NULL)

			// load psd file content
			if (EchoesReadPsd(s, assetPack)) {
				SendMessage(hMaterialsList, LB_RESETCONTENT, NULL, NULL);
				int idx = 0;
				for (auto& spritePair : assetPack.spriteSets) {
					auto &sprite = spritePair.second;
					std::string displayName = sprite.getBaseName();
					if (sprite.baseLayersData.size() > 1) {
						displayName += " (" + std::to_string(sprite.baseLayersData.size()) + " parts)";
					}
					if (sprite.lightLayerNames.size() > 0) {
						displayName += " (" + std::to_string(sprite.lightLayerNames.size()) + " light";
						if (sprite.lightLayerNames.size() > 1) {
							displayName += "s";
						}
						displayName += ")";
					}
					int pos = SendMessage(hMaterialsList, LB_ADDSTRING, NULL, (LPARAM)displayName.c_str());
					SendMessage(hMaterialsList, LB_SETITEMDATA, pos, (LPARAM)idx);
					idx++;
				}
				// select all
				uint32_t numItems = SendMessage(hMaterialsList, LB_GETCOUNT, NULL, NULL);
				uint32_t lparam = ((numItems-1) << 16) | 0;
				EXPECT(SendMessage(hMaterialsList, LB_SELITEMRANGE, TRUE, lparam) != LB_ERR, true)
				std::string msg = "Successfully loaded " + std::to_string(numItems) + " sprite(s) from PSD.";
				AppendGUILog(msg);
			} else {
				AppendGUILog("Failed to load PSD.");
				assetPack = AssetPack(); // clear it anyway
			}
			InvalidateRect(hMaterialsList, NULL, true);
			UpdateWindow(hMaterialsList);
		}
	}
}

void CmdExportAssetPack() {

}

void CmdPsdFormattingDoc() {
	ShellExecute(0, 0, _T("https://docs.google.com/document/d/1rUcAj-sK-fXPAnCBTwqrQdLIPcEDpZkJkXL9nP_7WYQ/edit?usp=sharing"), 0, 0, SW_SHOW);
}

void TestCommand() {

}

int WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(wcex.hInstance, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

	if (!RegisterClassEx(&wcex))
	{
		MessageBox(NULL,
				   _T("Call to RegisterClassEx failed!"),
				   _T("Windows Desktop Guided Tour"),
				   NULL);

		return 1;
	}

	// Store instance handle in our global variable
	hInst = hInstance;

	// The parameters to CreateWindowEx explained:
	// WS_EX_OVERLAPPEDWINDOW : An optional extended window style.
	// szWindowClass: the name of the application
	// szTitle: the text that appears in the title bar
	// WS_OVERLAPPEDWINDOW: the type of window to create
	// CW_USEDEFAULT, CW_USEDEFAULT: initial position (x, y)
	// 500, 100: initial size (width, length)
	// NULL: the parent of this window
	// NULL: this application does not have a menu bar
	// hInstance: the first parameter from WinMain
	// NULL: not used in this application
	HWND hWnd = CreateWindowEx(
		WS_EX_OVERLAPPEDWINDOW,
		szWindowClass,
		szTitle,
		WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME,
		CW_USEDEFAULT, CW_USEDEFAULT,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		NULL,
		NULL,
		hInstance,
		NULL
	);

	if (!hWnd)
	{
		MessageBox(NULL,
				   _T("Call to CreateWindow failed!"),
				   _T("Windows Desktop Guided Tour"),
				   NULL);

		return 1;
	}

	AddMenus(hWnd);
	AddContent(hWnd);

	// The parameters to ShowWindow explained:
	// hWnd: the value returned from CreateWindow
	// nCmdShow: the fourth parameter from WinMain
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	// Main message loop:
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
		case WM_COMMAND:
			switch (wParam)
			{
				case CMD_DEBUG:
					//TCHAR inputText[128];
					//GetWindowText(hInputText, inputText, 128);
					//LOG("%s", inputText)
					TestCommand();
					break;
				case CMD_OPEN_PSD:
					CmdOpenPsd();
					break;
				case CMD_PSD_FORMATTING_DOC:
					CmdPsdFormattingDoc();
					break;
				case CMD_EXPORT_ASSETPACK:
					CmdExportAssetPack();
					break;
			}
			break;
		case WM_PAINT:

			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
			break;
	}

	return 0;
}

// GetWindowText, SetWindowText

void AddMenus(HWND hWnd) {

	HMENU hFileMenu = CreateMenu();
	AppendMenu(hFileMenu, MF_STRING, CMD_OPEN_PSD, "Open");
	AppendMenu(hFileMenu, MF_STRING, CMD_EXPORT_ASSETPACK, "Export");

	HMENU hHelpMenu = CreateMenu();
	AppendMenu(hHelpMenu, MF_STRING, CMD_PSD_FORMATTING_DOC, "Formatting PSD for export");

	HMENU hMenu = CreateMenu();
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "File");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelpMenu, "Help");

	SetMenu(hWnd, hMenu);
}

void AddContent(HWND hWnd) {

	const uint32_t contentWidth = WINDOW_WIDTH - 2*GAP_MEDIUM - 2*WINDOW_PADDING;

	// info
	EXPECT(CreateWindowEx(
		0, WC_EDIT, _T(
			"1) Make sure you have your PSD formatted correctly\r\n"
			"  - see \"Help -> Formatting PSD for export\" for instructions\r\n"
			"2) Select your PSD from \"File -> Open\"\r\n"
			"3) Confirm the selected sprites you want to export, do \"File -> Export\"\r\n"
			"  - There should be no errors or warnings in the output log below.\r\n"
			"4) Submit the entire exported folder\r\n"
			"  - If you have perforce access, put it into \"Assets/03-Art Assets/AssetPacks\"\r\n"
			"  - Otherwise, upload it to the Echoes google drive\r\n"
			"\r\n"
			"At or DM Rain (discord: @miyehn) for questions or feedback or anything else!"
			),
		WS_VISIBLE | WS_CHILD | ES_READONLY | ES_MULTILINE,
		GAP_MEDIUM, GAP_LARGE, contentWidth, 180,
		hWnd, nullptr, nullptr, nullptr
	) != nullptr, true)

	// list (title)
	EXPECT(CreateWindowEx(
		0, WC_STATIC, _T(
			"Sprites to export:"
		),
		WS_VISIBLE | WS_CHILD,
		GAP_MEDIUM, GAP_LARGE + 200, contentWidth, 20,
		hWnd, nullptr, nullptr, nullptr
	) != nullptr, true)
	// list (content)
	hMaterialsList = CreateWindowEx(
		0, WC_LISTBOX, _T("materials list"),
		WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_MULTIPLESEL,
		GAP_MEDIUM, GAP_LARGE + 220, contentWidth, 200,
		hWnd, nullptr, nullptr, nullptr
	);
	EXPECT(hMaterialsList != nullptr, true)

	// log (title)
	EXPECT(CreateWindowEx(
		0, WC_STATIC, _T("Output Log"),
		WS_VISIBLE | WS_CHILD,
		GAP_MEDIUM, WINDOW_HEIGHT-260, contentWidth, 20,
		hWnd, nullptr, nullptr, nullptr
		) != nullptr, true)
	// log (content)
	hLogWnd = CreateWindowEx(
		0, WC_EDIT, _T(""),
		WS_VISIBLE | WS_CHILD | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER,
		GAP_MEDIUM, WINDOW_HEIGHT-240, contentWidth, 170,
		hWnd, nullptr, nullptr, nullptr
		);
	EXPECT(hLogWnd!=nullptr, true)

	/*
	EXPECT(CreateWindowEx(
		0, WC_BUTTON, _T("debug btn"),
		WS_VISIBLE | WS_CHILD,
		30, 120, 80, 30,
		hWnd, (HMENU)CMD_DEBUG, nullptr, nullptr
		) != nullptr, true)
	*/
}