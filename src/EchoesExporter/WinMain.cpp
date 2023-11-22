//
// Created by miyehn on 11/21/2023.
//
// HelloWindowsDesktop.cpp
// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c

#include <windows.h>
#include <shobjidl.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include "Log.h"

// Global variables

// The main window class name.
static TCHAR szWindowClass[] = _T("EchoesExporter");
// The string that appears in the application's title bar.
static TCHAR szTitle[] = _T("Echoes Exporter");

// Stored instance handle for use in Win32 API calls such as FindResource
HINSTANCE hInst;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

///////////////////////////////////////////////

#define CMD_DEBUG 1

static HWND hInputText = NULL;
static HWND hList = NULL;
void AddContent(HWND hWnd);

void TestCommand() {
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
			LOG("%s", s.c_str())

			if (hList != NULL) {
				// https://learn.microsoft.com/en-us/windows/win32/controls/create-a-simple-list-box
				int pos = SendMessage(hList, LB_ADDSTRING, NULL, (LPARAM)s.c_str());
				uint32_t numItems = SendMessage(hList, LB_GETCOUNT, NULL, NULL);
				SendMessage(hList, LB_SETITEMDATA, pos, (LPARAM)(numItems-1));
				//int numSelected = SendMessage(hList, LB_GETSELCOUNT, NULL, NULL);

				// select all
				uint32_t lparam = ((numItems-1) << 16) | 0;
				EXPECT(SendMessage(hList, LB_SELITEMRANGE, TRUE, lparam) != LB_ERR, true)
				LOG("%i items total", numItems)
			}
		}
	}

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
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		500, 300,
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
	TCHAR greeting[] = _T("Hello, Windows desktop!");

	switch (message)
	{
		case WM_CREATE:
			break;
		case WM_INITDIALOG:
			LOG("init dialog")
			break;
		case WM_COMMAND:
			switch (wParam)
			{
				case CMD_DEBUG:
					TCHAR inputText[128];
					GetWindowText(hInputText, inputText, 128);
					LOG("%s", inputText)
					TestCommand();
					break;
			}
			break;
		case WM_PAINT:
			hdc = BeginPaint(hWnd, &ps);

			// Here your application is laid out.
			// For this introduction, we just print out "Hello, Windows desktop!"
			// in the top left corner.
			TextOut(hdc,
					5, 5,
					greeting, _tcslen(greeting));
			// End application-specific layout section.

			EndPaint(hWnd, &ps);
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

void AddContent(HWND hWnd) {

	HWND hDesc = CreateWindowEx(
	0, WC_EDIT, _T("ababad fdasdfasf dasifaslf text wrap? more text more text\nmore text more text\n more text more text text text"),
		WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY,
		30, 31, 200, 50,
		hWnd, nullptr, nullptr, nullptr
	);
	EXPECT(hDesc != nullptr, true)

	hInputText = CreateWindowEx(
		0,_T("edit"), _T("edit me"),
		WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER,
		30, 61, 200, 50,
		hWnd,nullptr,nullptr,nullptr
	);
	EXPECT(hInputText != nullptr, true)

	EXPECT(CreateWindowEx(
		0, WC_BUTTON, _T("debug log"),
		WS_VISIBLE | WS_CHILD,
		30, 120, 80, 30,
		hWnd, (HMENU)CMD_DEBUG, nullptr, nullptr
		) != nullptr, true)

	hList = CreateWindowEx(
	0, WC_LISTBOX, _T("uhh"),
		WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_MULTIPLESEL,
		20, 150, 200, 60,
		hWnd, nullptr, nullptr, nullptr
	);
	EXPECT(hList != nullptr, true)
}