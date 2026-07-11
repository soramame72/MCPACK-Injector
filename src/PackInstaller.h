// PackInstaller.h - MCPACK Injector Install Thread
#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "PackDetector.h"

// =====================================================
// Pack Entry
// =====================================================
struct PackEntry
{
    std::wstring path;
    std::wstring name;
    PackType     type;
    int          status;
};

// =====================================================
// Install Thread Params
// =====================================================
struct InstallParams
{
    HWND                   hWnd;
    std::vector<PackEntry> entries;
    std::wstring           sevenZipPath;
    std::wstring           resourcePacksPath;
    std::wstring           behaviorPacksPath;
};

// =====================================================
// Messages
// =====================================================
#define WM_INSTALL_PROGRESS  (WM_APP + 1)
#define WM_INSTALL_STATUS    (WM_APP + 2)
#define WM_INSTALL_DONE      (WM_APP + 3)
#define WM_INSTALL_ITEM_UPD  (WM_APP + 4)

namespace PackInstaller
{
    DWORD WINAPI InstallThread(LPVOID param);
}
