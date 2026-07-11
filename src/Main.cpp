// Main.cpp - MCPACK Injector Entry Point
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>

#include "MainWindow.h"
#include "Settings.h"

// Enable visual styles and Common Controls v6 for SysLink
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

// ===================================================================
// WinMain Entry Point
// ===================================================================
int WINAPI WinMain(HINSTANCE hInst,
                   HINSTANCE hPrevInst,
                   LPSTR     lpCmdLine,
                   int       nCmdShow)
{
    // Initialize COM for SHBrowseForFolder BIF_NEWDIALOGSTYLE
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // Initialize common controls
    INITCOMMONCONTROLSEX icx = {0};
    icx.dwSize = sizeof(icx);
    icx.dwICC  = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&icx);

    // Load settings
    Settings::Load();

    // Register class
    if (!MainWindow::RegisterMainClass(hInst))
    {
        MessageBoxW(NULL,
            L"\u30A6\u30A3\u30F3\u30C9\u30A6\u30AF\u30E9\u30B9\u306E"
            L"\u767B\u9332\u306B\u5931\u6557\u3057\u307E\u3057\u305F",
            L"MCPACK Injector", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Create main window
    HWND hWnd = MainWindow::Create(hInst, nCmdShow);
    if (!hWnd)
    {
        MessageBoxW(NULL,
            L"\u30A6\u30A3\u30F3\u30C9\u30A6\u306E\u4F5C\u6210\u306B"
            L"\u5931\u6557\u3057\u307E\u3057\u305F",
            L"MCPACK Injector", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Message loop
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
