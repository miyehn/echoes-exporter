//
// Created by miyehn on 11/21/2023.
//
// HelloWindowsDesktop.cpp
// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <stdlib.h>
#include <vector>
#include <tchar.h>
#include "AssetPack.h"
#include "Log.h"

// Global variables

// The main window class name.
static TCHAR szWindowClass[] = _T("EchoesExporter");
// The string that appears in the application's title bar.
static TCHAR szTitle[] = _T("Echoes Exporter (version: 12/23/23)");

// Stored instance handle for use in Win32 API calls such as FindResource
HINSTANCE hInst;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

///////////////////////////////////////////////

#define CMD_DEBUG 1
#define CMD_OPEN_PSD 2
#define CMD_PSD_FORMATTING_DOC 3
#define CMD_BROWSE_SAVE_DIRECTORY 4
#define CMD_SPRITECONVERTER_DOC 5
#define CMD_EXPORT_ASSETPACK 6
#define CMD_RELOAD_LAST_PSD_AND_EXPORT 7

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 720
#define WINDOW_PADDING 10
#define LOG_HEIGHT 200
#define GAP_SMALL 5
#define GAP_MEDIUM 10
#define GAP_LARGE 20


static std::string GUILog;
static HWND hLogWnd = NULL;
static HWND hRootWnd = NULL;
static HWND hSaveDirectory = NULL;
static HWND hAssetPackName = NULL;
void AppendToGUILog(const GUILogEntry &entry, bool clearFirst) {
	if (clearFirst) GUILog = "";
	GUILog += entry.msg;
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

static GUID openPsdGUID = {
	1700158156, 55139, 19343, {137, 123, 65, 113}
};
static GUID exportPathGUID = {
	1308487700, 50820, 18963, {143, 61, 205, 250}
};

bool ReadPsd(const std::string& InFilePath, bool useLastPath = false) {
	assetPack = AssetPack(); // clear it first
	static std::string lastFilePath;
	std::string filePath;
	if (useLastPath) {
		filePath = lastFilePath;
	} else {
		filePath = InFilePath;
		lastFilePath = InFilePath;
	}
	// load psd file content
	bool success = EchoesReadPsd(filePath, assetPack);
	if (success) {
		SendMessage(hMaterialsList, LB_RESETCONTENT, NULL, NULL);
		int spritesCount = 0;
		for (auto& spritePair : assetPack.spriteSets) {
			auto &sprite = spritePair.second;
			std::string displayName = sprite.getBaseName();
			if (sprite.baseLayersData.size() > 1) {
				displayName += " (" + std::to_string(sprite.baseLayersData.size()) + " parts)";
			}
			if (sprite.emissionMaskData.size() > 0) {
				displayName += " (w emission)";
			}
			if (sprite.lightLayerNames.size() > 0) {
				displayName += " (" + std::to_string(sprite.lightLayerNames.size()) + " light";
				if (sprite.lightLayerNames.size() > 1) {
					displayName += "s";
				}
				displayName += ")";
			}
			int pos = SendMessage(hMaterialsList, LB_ADDSTRING, NULL, (LPARAM)displayName.c_str());
			SendMessage(hMaterialsList, LB_SETITEMDATA, pos, (LPARAM)spritesCount);
			spritesCount++;
		}
		if (spritesCount > 0) {
			AppendToGUILog({LT_LOG, "Loaded " + std::to_string(spritesCount) + " sprite(s)."}, true);
		} else {
			AppendToGUILog({LT_WARNING, "WARNING: no sprites are loaded. Are you sure the PSD file is formatted correctly?"});
		}
	} else {
		AppendToGUILog({LT_ERROR, "Failed to load PSD. Make sure your PSD is formatted correctly. If you're still not sure why, contact Rain."});
	}
	InvalidateRect(hMaterialsList, NULL, true);
	UpdateWindow(hMaterialsList);
	return success;
}

void CmdOpenPsd() {
	static HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	EXPECT(SUCCEEDED(hr), true)

	IFileOpenDialog *pFileOpen;
	EXPECT(SUCCEEDED(CoCreateInstance(
		CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen))), true);
	pFileOpen->SetClientGuid(openPsdGUID);

	const COMDLG_FILTERSPEC fileTypes[] = {{L"PSD documents (*.psd)", L"*.psd"}};
	if (!SUCCEEDED(pFileOpen->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes))) return;

	if (SUCCEEDED(pFileOpen->Show(NULL))) {
		IShellItem* pItem;
		if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
			PWSTR pszFilePath;
			EXPECT(SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)), true)

			std::wstring ws(pszFilePath);
			std::string s(ws.begin(), ws.end());
			LOG("reading PSD: %s", s.c_str())

			ASSERT(hMaterialsList != NULL)
			ReadPsd(s);
		}
	}
}

void CmdBrowseSaveDirectory() {
	static HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	EXPECT(SUCCEEDED(hr), true)

	IFileOpenDialog *pFileDialog;
	if (!SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileDialog)))) return;
	pFileDialog->SetClientGuid(exportPathGUID);

	DWORD curOptions;
	if (!SUCCEEDED(pFileDialog->GetOptions(&curOptions))) return;
	if (!SUCCEEDED(pFileDialog->SetOptions(curOptions | FOS_PICKFOLDERS))) return;

	if (SUCCEEDED(pFileDialog->Show(NULL))) {
		IShellItem* pItem;
		if (SUCCEEDED(pFileDialog->GetResult(&pItem))) {
			PWSTR pszFilePath;
			EXPECT(SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)), true)
			std::wstring ws(pszFilePath);
			std::string s(ws.begin(), ws.end());
			LOG("save path: %s", s.c_str())
			if (hSaveDirectory != NULL) {
				SetWindowText(hSaveDirectory, _T(s.c_str()));
				InvalidateRect(hSaveDirectory, NULL, true);
				UpdateWindow(hSaveDirectory);
			}
		}
	}
}

void ShowMessage(const std::string& text, const std::string& caption) {
	int msgbox = MessageBoxA(
		hRootWnd,
		_T(text.c_str()),
		_T(caption.c_str()),
		MB_ICONWARNING | MB_OK);

	switch(msgbox)
	{
		case IDOK:
			break;
		default:
			break;
	}
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
		return !std::isspace(ch);
	}));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
		return !std::isspace(ch);
	}).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
	rtrim(s);
	ltrim(s);
}

void CmdExportAssetPack() {
	if (assetPack.spriteSets.size() == 0) {
		ShowMessage( "Did you load the PSD file correctly?", "No sprites to export");
		return;
	}

	TCHAR buf[512];
	GetWindowText(hSaveDirectory, buf, 512);
	std::string outDirectory(buf);
	DWORD ftyp = GetFileAttributesA(outDirectory.c_str());
	if (ftyp == INVALID_FILE_ATTRIBUTES || !(ftyp & FILE_ATTRIBUTE_DIRECTORY)) {
		ShowMessage( "You need to first set export destination by clicking \"Browse..\"", "Invalid destination");
		return;
	}
	GetWindowText(hAssetPackName, buf, 512);
	std::string assetPackName(buf);
	trim(assetPackName);
	if (assetPackName.length() == 0) {
		ShowMessage( "Please specify a non-empty asset pack name, something like \"DarkwoodRocks\"", "Invalid asset pack name");
		return;
	}
	std::string fullPath = outDirectory + "\\" + assetPackName;

	if (ExportAssetPack(assetPack, fullPath, 0)) {
		AppendToGUILog({LT_SUCCESS, "Exported " + std::to_string(assetPack.spriteSets.size()) + " sprites to:"});
		AppendToGUILog({LT_SUCCESS, fullPath});
		AppendToGUILog({LT_LOG, "If you have build access, continue by following \"Help -> Configuring sprites in Unity\". Otherwise, submit the above folder to our google drive."});
	} else {
		AppendToGUILog({LT_ERROR, "Failed to export asset pack. Check the warnings/errors above (if any). Still not sure why? DM me (Rain) your psd file and let me take a look"});
	}
}

void CmdPsdFormattingDoc() {
	ShellExecute(0, 0, _T("https://docs.google.com/document/d/1rUcAj-sK-fXPAnCBTwqrQdLIPcEDpZkJkXL9nP_7WYQ/edit?usp=sharing"), 0, 0, SW_SHOW);
}

void CmdSpriteConverterDoc() {
	ShellExecute(0, 0, _T("https://docs.google.com/document/d/1PGAtx2exCd0odrkpP0Dy70wVBlfR2gZaDSlqFCF5uHM/edit?usp=sharing"), 0, 0, SW_SHOW);
}

void CmdReloadLastPsdAndExport() {
	if (ReadPsd("", true)) {
		CmdExportAssetPack();
	}
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
		0,
		szWindowClass,
		szTitle,
		WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX,
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
	hRootWnd = hWnd;

	AddMenus(hWnd);
	AddContent(hWnd);

	ShowWindow(hWnd, nCmdShow);
	InvalidateRect(hWnd, NULL, true);
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
					TestCommand();
					break;
				case CMD_OPEN_PSD:
					CmdOpenPsd();
					break;
				case CMD_PSD_FORMATTING_DOC:
					CmdPsdFormattingDoc();
					break;
				case CMD_BROWSE_SAVE_DIRECTORY:
					CmdBrowseSaveDirectory();
					break;
				case CMD_EXPORT_ASSETPACK:
					CmdExportAssetPack();
					break;
				case CMD_SPRITECONVERTER_DOC:
					CmdSpriteConverterDoc();
					break;
				case CMD_RELOAD_LAST_PSD_AND_EXPORT:
					CmdReloadLastPsdAndExport();
					break;
			}
			break;
		case WM_DROPFILES:
		{
			HDROP hDrop = reinterpret_cast<HDROP>(wParam);
			TCHAR fileName[512];
			uint32_t filesCount = DragQueryFile(hDrop, -1, fileName, 1024);
			if (filesCount && DragQueryFile(hDrop, 0, fileName, 1024)) {
				std::string s(fileName);
				ReadPsd(s);
			}
			break;
		}
		case WM_DESTROY:
		{
			// delete solid brush?
			PostQuitMessage(0);
			break;
		}
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
			break;
	}
	return 0;
}

void AddMenus(HWND hWnd) {

	HMENU hFileMenu = CreateMenu();
	AppendMenu(hFileMenu, MF_STRING, CMD_OPEN_PSD, "Open");
	//AppendMenu(hFileMenu, MF_STRING, CMD_BROWSE_SAVE_DIRECTORY, "Export");

	HMENU hHelpMenu = CreateMenu();
	AppendMenu(hHelpMenu, MF_STRING, CMD_PSD_FORMATTING_DOC, "Formatting PSD for export");
	AppendMenu(hHelpMenu, MF_STRING, CMD_SPRITECONVERTER_DOC, "Configuring sprites in Unity");

	HMENU hMenu = CreateMenu();
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "File");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelpMenu, "Help");

	SetMenu(hWnd, hMenu);
}

void AddContent(HWND hWnd) {

	const uint32_t horizontalPadding = GAP_MEDIUM;
	const uint32_t contentWidth = WINDOW_WIDTH - 2*horizontalPadding - 2*WINDOW_PADDING;

	uint32_t currentHeight = GAP_MEDIUM;
	// info
	EXPECT(CreateWindowEx(
		0, WC_EDIT, _T(
			"1) Make sure you have your PSD formatted correctly\r\n"
			"  - see \"Help -> Formatting PSD for export\" for instructions\r\n"
			"2) Select your PSD from \"File -> Open\" or drag it to the box below\r\n"
			"3) Specify export destination and asset pack name, and hit \"Export\"\r\n"
			"  - or use the button below to re-export your last PSD\r\n"
			"4) Submit the entire exported folder by following instructions in the output log\r\n"
			"\r\n"
			"At or DM Rain (discord: @miyehn) for questions or feedback or anything else!"
			),
		WS_VISIBLE | WS_CHILD | ES_READONLY | ES_MULTILINE,
		horizontalPadding, currentHeight, contentWidth, 130,
		hWnd, nullptr, nullptr, nullptr
	) != nullptr, true)
	currentHeight += 130 + GAP_MEDIUM;

	// list (title)
	EXPECT(CreateWindowEx(
		0, WC_STATIC, _T(
			"Sprites to export:"
		),
		WS_VISIBLE | WS_CHILD,
		horizontalPadding, currentHeight, contentWidth, 20,
		hWnd, nullptr, nullptr, nullptr
	) != nullptr, true)
	currentHeight += 20;
	// list (content)
	hMaterialsList = CreateWindowEx(
		WS_EX_ACCEPTFILES, WC_LISTBOX, _T("materials list"),
		WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOSEL | WS_VSCROLL,
		horizontalPadding, currentHeight, contentWidth, 170,
		hWnd, nullptr, nullptr, nullptr
	);
	EXPECT(hMaterialsList != nullptr, true)
	currentHeight += 170;

	const uint32_t descrLen = 140;
	const uint32_t btnLen = 100;
	{ // save destination
		EXPECT(CreateWindowEx(
			0, WC_STATIC, _T(
				"Export destination:"
			),
			WS_VISIBLE | WS_CHILD,
			horizontalPadding, currentHeight, descrLen, 20,
			hWnd, nullptr, nullptr, nullptr
		) != nullptr, true)
		hSaveDirectory = CreateWindowEx(
			0, WC_EDIT, _T(""),
			WS_VISIBLE | WS_CHILD | ES_READONLY | ES_AUTOHSCROLL,
			horizontalPadding + descrLen, currentHeight, contentWidth-descrLen-btnLen-GAP_SMALL, 20,
			hWnd, nullptr, nullptr, nullptr);
		EXPECT(hSaveDirectory != nullptr, true)
		EXPECT(CreateWindowEx(
			0, WC_BUTTON, _T("Browse.."),
			WS_VISIBLE | WS_CHILD,
			WINDOW_WIDTH-2*WINDOW_PADDING-horizontalPadding-btnLen, currentHeight, btnLen, 20,
			hWnd, (HMENU)CMD_BROWSE_SAVE_DIRECTORY, nullptr, nullptr
		) != nullptr, true)
		currentHeight += 20;
	}
	{// asset pack name
		EXPECT(CreateWindowEx(
			0, WC_STATIC, _T(
				"Asset pack name:"
			),
			WS_VISIBLE | WS_CHILD,
			horizontalPadding, currentHeight, descrLen, 20,
			hWnd, nullptr, nullptr, nullptr
		) != nullptr, true)
		hAssetPackName = CreateWindowEx(
			0, WC_EDIT, _T(""),
			WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | WS_BORDER,
			horizontalPadding + descrLen, currentHeight, contentWidth-descrLen-btnLen-GAP_SMALL, 20,
			hWnd, nullptr, nullptr, nullptr);
		EXPECT(hAssetPackName != nullptr, true)
		EXPECT(CreateWindowEx(
			0, WC_BUTTON, _T("Export"),
			WS_VISIBLE | WS_CHILD,
			WINDOW_WIDTH-2*WINDOW_PADDING-horizontalPadding-btnLen, currentHeight, btnLen, 20,
			hWnd, (HMENU)CMD_EXPORT_ASSETPACK, nullptr, nullptr
		) != nullptr, true)
		currentHeight += 20 + GAP_MEDIUM;
	}

	{// Reload last PSD document and export
		EXPECT(CreateWindowEx(
			0, WC_STATIC, _T("Or:"),
			WS_VISIBLE | WS_CHILD,
			horizontalPadding, currentHeight, 60, 20,
			hWnd, nullptr, nullptr, nullptr
		) != nullptr, true)
		EXPECT(CreateWindowEx(
			0, WC_BUTTON, _T("Reload last PSD document and export to above destination"),
			WS_VISIBLE | WS_CHILD,
			60 + horizontalPadding, currentHeight, contentWidth - 60, 20,
			hWnd, (HMENU)CMD_RELOAD_LAST_PSD_AND_EXPORT, nullptr, nullptr
		) != nullptr, true)
		currentHeight += 20 + GAP_MEDIUM;
	}

	// log (title)
	EXPECT(CreateWindowEx(
		0, WC_STATIC, _T("Output Log"),
		WS_VISIBLE | WS_CHILD,
		horizontalPadding, currentHeight, contentWidth, 20,
		hWnd, nullptr, nullptr, nullptr
		) != nullptr, true)
	currentHeight += 20;

	// log (content)
	hLogWnd = CreateWindowEx(
		0, WC_EDIT, _T(""),
		WS_VISIBLE | WS_CHILD | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER,
		horizontalPadding, currentHeight, contentWidth, LOG_HEIGHT,
		hWnd, nullptr, nullptr, nullptr
		);
	EXPECT(hLogWnd!=nullptr, true)
}