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
    std::wstring errorMsg;
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
    std::wstring           skinPacksPath;
    std::wstring           worldsPath;
    std::wstring           worldTemplatesPath;
};

// =====================================================
// Collision Info
// =====================================================
struct CollisionInfo
{
    const wchar_t* fileName;
    const wchar_t* existingPath;
};

// =====================================================
// Messages
// =====================================================
#define WM_INSTALL_PROGRESS      (WM_APP + 1)
#define WM_INSTALL_STATUS        (WM_APP + 2)
#define WM_INSTALL_DONE          (WM_APP + 3)
#define WM_INSTALL_ITEM_UPD      (WM_APP + 4)
#define WM_INSTALL_DETAIL_PROG   (WM_APP + 5)
#define WM_INSTALL_DETAIL_STATUS (WM_APP + 6)
#define WM_INSTALL_UUID_COLLISION (WM_APP + 7)

namespace PackInstaller
{
    DWORD WINAPI InstallThread(LPVOID param);
}
