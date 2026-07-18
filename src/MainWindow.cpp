// MainWindow.cpp - MCPACK Injector Main Window Implementation
#include "MainWindow.h"
#include "PackDetector.h"
#include "PackInstaller.h"
#include "Settings.h"
#include "resource.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <commctrl.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

// ===================================================================
// Constants
// ===================================================================
static const wchar_t* const WNDCLASS_MAIN    = L"MCPackInjectorMain";
static const wchar_t* const WNDCLASS_OPTIONS = L"MCPackInjectorOpt";
static const wchar_t* const WNDCLASS_ABOUT   = L"MCPackInjectorAbout";
static const wchar_t* const WNDCLASS_UNKNOWN = L"MCPackInjectorUnknown";
static const wchar_t* const WNDCLASS_COLLISION = L"MCPackInjectorCollision";

static const int CLIENT_W = 492;
static const int CLIENT_H = 355; // Height increased by 50px for detailed progress

// ===================================================================
// Globals
// ===================================================================
namespace MainWindow
{
    HINSTANCE           g_hInst    = NULL;
    HWND                g_hWnd     = NULL;
    std::vector<PackEntry> g_entries;
    bool                g_installing = false;
    volatile bool       g_cancelRequested = false;
}

static HWND s_hList    = NULL;
static HWND s_hEdit    = NULL;
static HWND s_hBrowse  = NULL;
static HWND s_hAddPath = NULL;
static HWND s_hRemove  = NULL;
static HWND s_hClear   = NULL;
static HWND s_hInstall = NULL;
static HWND s_hProg    = NULL;
static HWND s_hStatus  = NULL;
static HWND s_hProgDetail = NULL;
static HWND s_hStatusDetail = NULL;

// ===================================================================
// Options State
// ===================================================================
struct OptionsDlgState
{
    std::wstring resPath;
    std::wstring behPath;
    std::wstring skinPath;
    std::wstring wldPath;
    bool         ok;
    HWND         hEditRes;
    HWND         hEditBeh;
    HWND         hEditSkin;
    HWND         hEditWld;
};

// ===================================================================
// Utilities
// ===================================================================
static HFONT GetModernFont()
{
    NONCLIENTMETRICSW ncm = {0};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

static std::wstring ExtractFileName(const std::wstring& path)
{
    const wchar_t* p = PathFindFileNameW(path.c_str());
    return p ? std::wstring(p) : path;
}

static void ListViewAddEntry(int index, const PackEntry& e)
{
    LVITEMW lvi = {0};
    lvi.mask    = LVIF_TEXT;
    lvi.iItem   = index;
    lvi.iSubItem = 0;
    lvi.pszText = const_cast<wchar_t*>(e.name.c_str());
    ListView_InsertItem(s_hList, &lvi);

    ListView_SetItemText(s_hList, index, 1,
        const_cast<wchar_t*>(PackDetector::GetTypeName(e.type)));

    ListView_SetItemText(s_hList, index, 2,
        const_cast<wchar_t*>(L"\u5F85\u6A5F\u4E2D"));
}

static void ListViewUpdateStatus(int index, int status)
{
    const wchar_t* txt = NULL;
    switch (status)
    {
    case 0: txt = L"\u5F85\u6A5F\u4E2D"; break;
    case 1: txt = L"\u51E6\u7406\u4E2D"; break;
    case 2: txt = L"\u5B8C\u4E86";       break;
    case 3: txt = L"\u30A8\u30E9\u30FC"; break;
    default: txt = L"?";
    }
    ListView_SetItemText(s_hList, index, 2, const_cast<wchar_t*>(txt));
}

static void ListViewUpdateType(int index, PackType type)
{
    ListView_SetItemText(s_hList, index, 1,
        const_cast<wchar_t*>(PackDetector::GetTypeName(type)));
}

static void SetControlsEnabled(bool enabled)
{
    EnableWindow(s_hBrowse,  enabled ? TRUE : FALSE);
    EnableWindow(s_hEdit,    enabled ? TRUE : FALSE);
    EnableWindow(s_hAddPath, enabled ? TRUE : FALSE);
    EnableWindow(s_hRemove,  enabled ? TRUE : FALSE);
    EnableWindow(s_hClear,   enabled ? TRUE : FALSE);
    // Do not disable Install button, it becomes Cancel button
    // EnableWindow(s_hInstall, enabled ? TRUE : FALSE);

    HMENU hMenu = GetMenu(MainWindow::g_hWnd);
    if (hMenu)
    {
        UINT flag = enabled ? MF_ENABLED : MF_GRAYED;
        EnableMenuItem(hMenu, IDM_FILE_ADD,   MF_BYCOMMAND | flag);
        EnableMenuItem(hMenu, IDM_FILE_CLEAR, MF_BYCOMMAND | flag);
        DrawMenuBar(MainWindow::g_hWnd);
    }
}

// ===================================================================
// Add file
// ===================================================================
static void AddPackFile(const std::wstring& path)
{
    const wchar_t* pExt = PathFindExtensionW(path.c_str());
    if (!pExt) return;
    if (_wcsicmp(pExt, L".mcpack")  != 0 &&
        _wcsicmp(pExt, L".mcaddon") != 0 &&
        _wcsicmp(pExt, L".zip")     != 0)
    {
        return;
    }

    for (size_t i = 0; i < MainWindow::g_entries.size(); ++i)
    {
        if (_wcsicmp(MainWindow::g_entries[i].path.c_str(), path.c_str()) == 0)
            return;
    }

    PackEntry e;
    e.path   = path;
    e.name   = ExtractFileName(path);
    e.status = 0;

    if (_wcsicmp(pExt, L".mcaddon") == 0)
        e.type = PACK_ADDON;
    else
        e.type = PackDetector::Detect(path, Settings::GetSevenZipPath());

    int idx = static_cast<int>(MainWindow::g_entries.size());
    MainWindow::g_entries.push_back(e);
    ListViewAddEntry(idx, e);
}

// ===================================================================
// Options Window Proc
// ===================================================================
static LRESULT CALLBACK OptionsDlgWndProc(HWND hWnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
    static OptionsDlgState* s_pState = NULL;

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* pcs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        s_pState = reinterpret_cast<OptionsDlgState*>(pcs->lpCreateParams);

        HFONT hFont = GetModernFont();

        HWND hSt1 = CreateWindowExW(0, L"STATIC",
            L"\u30EA\u30BD\u30FC\u30B9\u30D1\u30C3\u30AF"
            L"\u30D5\u30A9\u30EB\u30C0:",
            WS_CHILD | WS_VISIBLE,
            10, 12, 300, 16, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hSt1, WM_SETFONT, (WPARAM)hFont, TRUE);

        s_pState->hEditRes = CreateWindowExW(WS_EX_CLIENTEDGE,
            L"EDIT", s_pState->resPath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            10, 30, 330, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_EDIT_RES_PATH),
            MainWindow::g_hInst, NULL);
        SendMessage(s_pState->hEditRes, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBr1 = CreateWindowExW(0, L"BUTTON",
            L"\u53C2\u7167...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            345, 30, 70, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_BTN_RES_BROWSE),
            MainWindow::g_hInst, NULL);
        SendMessage(hBr1, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hSt2 = CreateWindowExW(0, L"STATIC",
            L"\u30D3\u30D8\u30A4\u30D3\u30A2\u30FC\u30D1\u30C3\u30AF"
            L"\u30D5\u30A9\u30EB\u30C0:",
            WS_CHILD | WS_VISIBLE,
            10, 62, 300, 16, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hSt2, WM_SETFONT, (WPARAM)hFont, TRUE);

        s_pState->hEditBeh = CreateWindowExW(WS_EX_CLIENTEDGE,
            L"EDIT", s_pState->behPath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            10, 80, 330, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_EDIT_BEH_PATH),
            MainWindow::g_hInst, NULL);
        SendMessage(s_pState->hEditBeh, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBr2 = CreateWindowExW(0, L"BUTTON",
            L"\u53C2\u7167...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            345, 80, 70, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_BTN_BEH_BROWSE),
            MainWindow::g_hInst, NULL);
        SendMessage(hBr2, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hSt3 = CreateWindowExW(0, L"STATIC",
            L"\u30B9\u30AD\u30F3\u30D1\u30C3\u30AF"
            L"\u30D5\u30A9\u30EB\u30C0:",
            WS_CHILD | WS_VISIBLE,
            10, 112, 300, 16, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hSt3, WM_SETFONT, (WPARAM)hFont, TRUE);

        s_pState->hEditSkin = CreateWindowExW(WS_EX_CLIENTEDGE,
            L"EDIT", s_pState->skinPath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            10, 130, 330, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_EDIT_SKIN_PATH),
            MainWindow::g_hInst, NULL);
        SendMessage(s_pState->hEditSkin, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBr3 = CreateWindowExW(0, L"BUTTON",
            L"\u53C2\u7167...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            345, 130, 70, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_BTN_SKIN_BROWSE),
            MainWindow::g_hInst, NULL);
        SendMessage(hBr3, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hSt4 = CreateWindowExW(0, L"STATIC",
            L"\u30EF\u30FC\u30EB\u30C9"
            L"\u30D5\u30A9\u30EB\u30C0:",
            WS_CHILD | WS_VISIBLE,
            10, 162, 300, 16, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hSt4, WM_SETFONT, (WPARAM)hFont, TRUE);

        s_pState->hEditWld = CreateWindowExW(WS_EX_CLIENTEDGE,
            L"EDIT", s_pState->wldPath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            10, 180, 330, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_EDIT_WLD_PATH),
            MainWindow::g_hInst, NULL);
        SendMessage(s_pState->hEditWld, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBr4 = CreateWindowExW(0, L"BUTTON",
            L"\u53C2\u7167...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            345, 180, 70, 22, hWnd,
            reinterpret_cast<HMENU>(IDC_BTN_WLD_BROWSE),
            MainWindow::g_hInst, NULL);
        SendMessage(hBr4, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hOK = CreateWindowExW(0, L"BUTTON",
            L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            240, 215, 80, 26, hWnd,
            reinterpret_cast<HMENU>(IDC_OPT_OK),
            MainWindow::g_hInst, NULL);
        SendMessage(hOK, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hCancel = CreateWindowExW(0, L"BUTTON",
            L"\u30AD\u30E3\u30F3\u30BB\u30EB",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            328, 215, 90, 26, hWnd,
            reinterpret_cast<HMENU>(IDC_OPT_CANCEL),
            MainWindow::g_hInst, NULL);
        SendMessage(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);

        if (id == IDC_OPT_OK)
        {
            wchar_t buf[MAX_PATH] = {0};
            GetWindowTextW(s_pState->hEditRes, buf, MAX_PATH);
            s_pState->resPath = buf;

            GetWindowTextW(s_pState->hEditBeh, buf, MAX_PATH);
            s_pState->behPath = buf;

            GetWindowTextW(s_pState->hEditSkin, buf, MAX_PATH);
            s_pState->skinPath = buf;

            GetWindowTextW(s_pState->hEditWld, buf, MAX_PATH);
            s_pState->wldPath = buf;

            s_pState->ok = true;
            DestroyWindow(hWnd);
        }
        else if (id == IDC_OPT_CANCEL)
        {
            s_pState->ok = false;
            DestroyWindow(hWnd);
        }
        else if (id == IDC_BTN_RES_BROWSE || id == IDC_BTN_BEH_BROWSE || id == IDC_BTN_SKIN_BROWSE || id == IDC_BTN_WLD_BROWSE)
        {
            BROWSEINFOW bi = {0};
            bi.hwndOwner = hWnd;
            bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            bi.lpszTitle = L"\u30D5\u30A9\u30EB\u30C0\u3092\u9078\u629E"
                           L"\u3057\u3066\u304F\u3060\u3055\u3044";

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl)
            {
                wchar_t szPath[MAX_PATH] = {0};
                if (SHGetPathFromIDListW(pidl, szPath))
                {
                    if (id == IDC_BTN_RES_BROWSE)
                        SetWindowTextW(s_pState->hEditRes, szPath);
                    else if (id == IDC_BTN_BEH_BROWSE)
                        SetWindowTextW(s_pState->hEditBeh, szPath);
                    else if (id == IDC_BTN_SKIN_BROWSE)
                        SetWindowTextW(s_pState->hEditSkin, szPath);
                    else if (id == IDC_BTN_WLD_BROWSE)
                        SetWindowTextW(s_pState->hEditWld, szPath);
                }
                CoTaskMemFree(pidl);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        PostThreadMessage(GetCurrentThreadId(), WM_APP + 100, 0, 0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            if (s_pState) s_pState->ok = false;
            DestroyWindow(hWnd);
        }
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ===================================================================
// Show Options Dialog
// ===================================================================
static void ShowOptionsDialog(HWND hParent)
{
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = {0};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = OptionsDlgWndProc;
        wc.hInstance     = MainWindow::g_hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = WNDCLASS_OPTIONS;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_registered = true;
    }

    OptionsDlgState state;
    state.resPath  = Settings::resourcePacksPath;
    state.behPath  = Settings::behaviorPacksPath;
    state.skinPath = Settings::skinPacksPath;
    state.wldPath  = Settings::worldsPath;
    state.ok       = false;
    state.hEditRes = NULL;
    state.hEditBeh = NULL;
    state.hEditSkin = NULL;
    state.hEditWld  = NULL;

    RECT rcParent = {0};
    GetWindowRect(hParent, &rcParent);
    int dlgW = 430, dlgH = 290;
    int x = rcParent.left + (rcParent.right  - rcParent.left - dlgW) / 2;
    int y = rcParent.top  + (rcParent.bottom - rcParent.top  - dlgH) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    EnableWindow(hParent, FALSE);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WNDCLASS_OPTIONS,
        L"\u8A2D\u5B9A",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hParent, NULL, MainWindow::g_hInst, &state);

    if (!hDlg)
    {
        EnableWindow(hParent, TRUE);
        return;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        if (msg.message == WM_APP + 100 && msg.hwnd == NULL)
            break;

        if (!IsDialogMessage(hDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    if (state.ok)
    {
        Settings::resourcePacksPath = state.resPath;
        Settings::behaviorPacksPath = state.behPath;
        Settings::skinPacksPath     = state.skinPath;
        Settings::worldsPath        = state.wldPath;
        Settings::Save();
    }
}

// ===================================================================
// About Dialog Procedure
// ===================================================================
static LRESULT CALLBACK AboutDlgWndProc(HWND hWnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HFONT hFont = GetModernFont();

        // Title header
        HWND hSt1 = CreateWindowExW(0, L"STATIC",
            L"MCPACK Injector ver0.0.1x144\n"
            L"\u88FD\u4F5C\u8005: soramame72\n"
            L"\u00A92026 soramame72",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 15, 360, 50, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hSt1, WM_SETFONT, (WPARAM)hFont, TRUE);

        // soramame72 Website
        HWND hStWeb1 = CreateWindowExW(0, L"STATIC", L"soramame72 website:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 75, 360, 18, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hStWeb1, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hLink1 = CreateWindowExW(0, L"SysLink", L"<a href=\"http://mamechosu.cloudfree.jp\">http://mamechosu.cloudfree.jp</a>",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            20, 95, 360, 18, hWnd, reinterpret_cast<HMENU>(IDC_SYSLINK_URL), MainWindow::g_hInst, NULL);
        SendMessage(hLink1, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Divider
        HWND hStDiv = CreateWindowExW(0, L"STATIC", L"------------------------------------",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 125, 360, 15, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hStDiv, WM_SETFONT, (WPARAM)hFont, TRUE);

        // OSS License Description
        HWND hStOSS = CreateWindowExW(0, L"STATIC",
            L"\u3053\u306E\u30BD\u30D5\u30C8\u30A6\u30A7\u30A2\u306F\u4EE5\u4E0B\u306EOSS\u3092\u4F7F\u7528\u3057\u3066\u3044\u307E\u3059:\n"
            L"7-Zip \u00A9 Igor Pavlov (GNU LGPL)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 150, 360, 35, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hStOSS, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 7-Zip Website
        HWND hStWeb2 = CreateWindowExW(0, L"STATIC", L"7-Zip website:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 195, 360, 18, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hStWeb2, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hLink2 = CreateWindowExW(0, L"SysLink", L"<a href=\"https://www.7-zip.org/\">https://www.7-zip.org/</a>",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            20, 215, 360, 18, hWnd, reinterpret_cast<HMENU>(IDC_SYSLINK_URL + 1), MainWindow::g_hInst, NULL);
        SendMessage(hLink2, WM_SETFONT, (WPARAM)hFont, TRUE);

        // OK Button
        HWND hOK = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            160, 255, 80, 26, hWnd, reinterpret_cast<HMENU>(IDC_ABOUT_OK), MainWindow::g_hInst, NULL);
        SendMessage(hOK, WM_SETFONT, (WPARAM)hFont, TRUE);

        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ABOUT_OK)
            DestroyWindow(hWnd);
        return 0;

    case WM_NOTIFY:
    {
        LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
        if (pnmh->code == NM_CLICK || pnmh->code == NM_RETURN)
        {
            PNMLINK pNMLink = reinterpret_cast<PNMLINK>(lParam);
            if (pnmh->idFrom == IDC_SYSLINK_URL || pnmh->idFrom == (IDC_SYSLINK_URL + 1))
            {
                ShellExecuteW(NULL, L"open", pNMLink->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
                return TRUE;
            }
        }
        return 0;
    }

    case WM_DESTROY:
        PostThreadMessage(GetCurrentThreadId(), WM_APP + 101, 0, 0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
            DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ===================================================================
// About Dialog
// ===================================================================
static void ShowAboutDialog(HWND hParent)
{
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = {0};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = AboutDlgWndProc;
        wc.hInstance     = MainWindow::g_hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = WNDCLASS_ABOUT;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_registered = true;
    }

    RECT rcParent = {0};
    GetWindowRect(hParent, &rcParent);
    int dlgW = 400, dlgH = 340;
    int x = rcParent.left + (rcParent.right  - rcParent.left - dlgW) / 2;
    int y = rcParent.top  + (rcParent.bottom - rcParent.top  - dlgH) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    EnableWindow(hParent, FALSE);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WNDCLASS_ABOUT,
        L"\u30D0\u30FC\u30B8\u30E7\u30F3\u60C5\u5831",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hParent, NULL, MainWindow::g_hInst, NULL);

    if (hDlg)
    {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0) > 0)
        {
            if (msg.message == WM_APP + 101 && msg.hwnd == NULL)
                break;
            if (!IsDialogMessage(hDlg, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
}

// ===================================================================
// Browse Files
// ===================================================================
static void BrowseAndAdd(HWND hWnd)
{
    wchar_t szFiles[32768] = {0};

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    ofn.lpstrFilter  =
        L"Minecraft\u30D1\u30C3\u30AF\u30D5\u30A1\u30A4\u30EB"
        L" (*.mcpack;*.mcaddon;*.zip)\0"
        L"*.mcpack;*.mcaddon;*.zip\0"
        L"\u3059\u3079\u3066\u306E\u30D5\u30A1\u30A4\u30EB (*.*)\0*.*\0\0";
    ofn.lpstrFile    = szFiles;
    ofn.nMaxFile     = _countof(szFiles);
    ofn.Flags        = OFN_ALLOWMULTISELECT | OFN_EXPLORER |
                       OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle   = L"\u30D5\u30A1\u30A4\u30EB\u3092\u9078\u629E";

    if (!GetOpenFileNameW(&ofn))
        return;

    wchar_t* pDir = szFiles;
    wchar_t* pFile = szFiles + wcslen(szFiles) + 1;

    if (*pFile == L'\0')
    {
        AddPackFile(std::wstring(pDir));
    }
    else
    {
        std::wstring dir(pDir);
        while (*pFile)
        {
            std::wstring fullPath = dir + L"\\" + pFile;
            AddPackFile(fullPath);
            pFile += wcslen(pFile) + 1;
        }
    }
}

// ===================================================================
// Unknown Pack Confirmation Dialog
// ===================================================================
struct UnknownDlgState
{
    std::wstring fileName;
    PackType     selectedType;
};

static LRESULT CALLBACK UnknownDlgWndProc(HWND hWnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
    static UnknownDlgState* s_pState = NULL;

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* pcs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        s_pState = reinterpret_cast<UnknownDlgState*>(pcs->lpCreateParams);

        HFONT hFont = GetModernFont();

        std::wstring text = L"\u4EE5\u4E0B\u306E\u30D5\u30A1\u30A4\u30EB\u306E\u7A2E\u985E\u304C\u5224\u5225\u3067\u304D\u307E\u305B\u3093\u3067\u3057\u305F\u3002\n";
        text += L"(\u30DE\u30A4\u30AF\u30E9\u306B\u95A2\u4FC2\u306A\u3044\u30D5\u30A1\u30A4\u30EB\u306E\u53EF\u80FD\u6027\u3082\u3042\u308A\u307E\u3059)\n\n";
        text += L"\u30D5\u30A1\u30A4\u30EB: " + s_pState->fileName + L"\n\n";
        text += L"\u3069\u306E\u3088\u3046\u306B\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u3057\u307E\u3059\u304B\uFF1F";

        HWND hSt = CreateWindowExW(0, L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 15, 360, 70, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hSt, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnRes = CreateWindowExW(0, L"BUTTON", L"\u30EA\u30BD\u30FC\u30B9\u30D1\u30C3\u30AF\u3068\u3057\u3066\u5165\u308C\u308B",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            20, 95, 200, 26, hWnd, reinterpret_cast<HMENU>(101), MainWindow::g_hInst, NULL);
        SendMessage(hBtnRes, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnBeh = CreateWindowExW(0, L"BUTTON", L"\u30D3\u30D8\u30A4\u30D3\u30A2\u30FC\u30D1\u30C3\u30AF\u3068\u3057\u3066\u5165\u308C\u308B",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            20, 130, 200, 26, hWnd, reinterpret_cast<HMENU>(102), MainWindow::g_hInst, NULL);
        SendMessage(hBtnBeh, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnAddon = CreateWindowExW(0, L"BUTTON", L"\u30A2\u30C9\u30AA\u30F3\u3068\u3057\u3066\u5165\u308C\u308B",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            20, 165, 200, 26, hWnd, reinterpret_cast<HMENU>(104), MainWindow::g_hInst, NULL);
        SendMessage(hBtnAddon, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnSkip = CreateWindowExW(0, L"BUTTON", L"\u30B9\u30AD\u30C3\u30D7\u3059\u308B",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            240, 165, 130, 26, hWnd, reinterpret_cast<HMENU>(103), MainWindow::g_hInst, NULL);
        SendMessage(hBtnSkip, WM_SETFONT, (WPARAM)hFont, TRUE);

        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 101)
        {
            if (s_pState) s_pState->selectedType = PACK_RESOURCE;
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == 102)
        {
            if (s_pState) s_pState->selectedType = PACK_BEHAVIOR;
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == 104)
        {
            if (s_pState) s_pState->selectedType = PACK_ADDON;
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == 103)
        {
            if (s_pState) s_pState->selectedType = PACK_UNKNOWN;
            DestroyWindow(hWnd);
        }
        return 0;

    case WM_DESTROY:
        PostThreadMessage(GetCurrentThreadId(), WM_APP + 102, 0, 0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static PackType PromptUnknownPack(HWND hParent, const std::wstring& fileName)
{
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = {0};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = UnknownDlgWndProc;
        wc.hInstance     = MainWindow::g_hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = WNDCLASS_UNKNOWN;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_registered = true;
    }

    UnknownDlgState state;
    state.fileName = fileName;
    state.selectedType = PACK_UNKNOWN; // Default is skip

    RECT rcParent = {0};
    GetWindowRect(hParent, &rcParent);
    int dlgW = 400, dlgH = 245;
    int x = rcParent.left + (rcParent.right  - rcParent.left - dlgW) / 2;
    int y = rcParent.top  + (rcParent.bottom - rcParent.top  - dlgH) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    EnableWindow(hParent, FALSE);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WNDCLASS_UNKNOWN,
        L"\u7A2E\u985E\u306E\u78BA\u8A8D", // "種類の確認"
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hParent, NULL, MainWindow::g_hInst, &state);

    if (hDlg)
    {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0) > 0)
        {
            if (msg.message == WM_APP + 102 && msg.hwnd == NULL)
                break;
            if (!IsDialogMessage(hDlg, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    return state.selectedType;
}

// ===================================================================
// UUID Collision Dialog
// ===================================================================
struct CollisionDlgState
{
    const wchar_t* fileName;
    const wchar_t* existingPath;
    int result; // 1: Overwrite, 2: Skip, 3: Keep Both
};

static LRESULT CALLBACK UuidCollisionDlgWndProc(HWND hWnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam)
{
    static CollisionDlgState* s_pState = NULL;

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* pcs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        s_pState = reinterpret_cast<CollisionDlgState*>(pcs->lpCreateParams);

        HFONT hFont = GetModernFont();

        std::wstring text = L"\u91CD\u8907\u3057\u305F\u30D1\u30C3\u30AF (UUID)\u307E\u305F\u306F\u30D5\u30A9\u30EB\u30C0\u304C\u691C\u51FA\u3055\u308F\u307E\u3057\u305F\u3002\n\n";
        text += L"\u30D5\u30A1\u30A4\u30EB: ";
        text += s_pState->fileName;
        text += L"\n\u5DF1\u306B\u5B58\u5728\u3059\u308B\u30D5\u30A9\u30EB\u30C0: ";
        text += s_pState->existingPath;
        text += L"\n\n\u5BFE\u5FDC\u3092\u9078\u629E\u3057\u3066\u304F\u3060\u3055\u3044\u3002";

        HWND hSt = CreateWindowExW(0, L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 15, 360, 90, hWnd, NULL, MainWindow::g_hInst, NULL);
        SendMessage(hSt, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnOver = CreateWindowExW(0, L"BUTTON", L"\u4E0A\u66F8\u304D (Overwrite)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            20, 115, 160, 26, hWnd, reinterpret_cast<HMENU>(101), MainWindow::g_hInst, NULL);
        SendMessage(hBtnOver, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnSkip = CreateWindowExW(0, L"BUTTON", L"\u30B9\u30AD\u30C3\u30D7 (Skip)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            20, 150, 160, 26, hWnd, reinterpret_cast<HMENU>(102), MainWindow::g_hInst, NULL);
        SendMessage(hBtnSkip, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnKeep = CreateWindowExW(0, L"BUTTON", L"\u4E21\u65B9\u7DAD\u6301 (UUID\u5909\u66F4)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            200, 150, 170, 26, hWnd, reinterpret_cast<HMENU>(103), MainWindow::g_hInst, NULL);
        SendMessage(hBtnKeep, WM_SETFONT, (WPARAM)hFont, TRUE);

        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id >= 101 && id <= 103)
        {
            if (s_pState) s_pState->result = id - 100;
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_DESTROY:
        PostThreadMessage(GetCurrentThreadId(), WM_APP + 103, 0, 0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static LRESULT PromptUuidCollision(HWND hParent, CollisionInfo* pCi)
{
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = {0};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = UuidCollisionDlgWndProc;
        wc.hInstance     = MainWindow::g_hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = WNDCLASS_COLLISION;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_registered = true;
    }

    CollisionDlgState state;
    state.fileName = pCi->fileName;
    state.existingPath = pCi->existingPath;
    state.result = 2; // Default to skip

    RECT rcParent = {0};
    GetWindowRect(hParent, &rcParent);
    int dlgW = 400, dlgH = 225;
    int x = rcParent.left + (rcParent.right  - rcParent.left - dlgW) / 2;
    int y = rcParent.top  + (rcParent.bottom - rcParent.top  - dlgH) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    EnableWindow(hParent, FALSE);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WNDCLASS_COLLISION,
        L"\u91CD\u8907\u306E\u78BA\u8A8D",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hParent, NULL, MainWindow::g_hInst, &state);

    if (hDlg)
    {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0) > 0)
        {
            if (msg.message == WM_APP + 103 && msg.hwnd == NULL)
                break;
            if (!IsDialogMessage(hDlg, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    return state.result;
}

// ===================================================================
// Start Install
// ===================================================================
static void StartInstall(HWND hWnd)
{
    if (MainWindow::g_installing)
    {
        // If already installing, this acts as Cancel button
        MainWindow::g_cancelRequested = true;
        EnableWindow(s_hInstall, FALSE);
        SetWindowTextW(s_hInstall, L"\u30AD\u30E3\u30F3\u30BB\u30EB\u3057\u3066\u3044\u307E\u3059...");
        return;
    }
    if (MainWindow::g_entries.empty())   return;

    std::wstring szPath = Settings::GetSevenZipPath();
    if (!PathFileExistsW(szPath.c_str()))
    {
        MessageBoxW(hWnd,
            L"\u30A8\u30E9\u30FC: 7z.exe \u304C\u898B\u3064\u304B\u308A"
            L"\u307E\u305B\u3093\u3002\n"
            L"\u5B9F\u884C\u30D5\u30A1\u30A4\u30EB\u3068\u540C\u3058"
            L"\u30D5\u30A9\u30EB\u30C0\u306E 7z\\ \u306B\u7F6E\u3044"
            L"\u3066\u304F\u3060\u3055\u3044\u3002",
            L"MCPACK Injector",
            MB_OK | MB_ICONERROR);
        return;
    }

    if (!PathFileExistsW(Settings::resourcePacksPath.c_str()) ||
        !PathFileExistsW(Settings::behaviorPacksPath.c_str()) ||
        !PathFileExistsW(Settings::skinPacksPath.c_str()) ||
        !PathFileExistsW(Settings::worldsPath.c_str()))
    {
        int ret = MessageBoxW(hWnd,
            L"\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u5148\u30D5\u30A9"
            L"\u30EB\u30C0\u304C\u898B\u3064\u304B\u308A\u307E\u305B"
            L"\u3093\u3002\n\u30AA\u30D7\u30B7\u30E7\u30F3\u3067\u8A2D"
            L"\u5B9A\u3092\u78BA\u8A8D\u3057\u3066\u304F\u3060\u3055\u3044"
            L"\u3002\n\n\u305D\u306E\u307E\u307E\u7D9A\u884C\u3057\u307E"
            L"\u3059\u304B\uFF1F",
            L"MCPACK Injector",
            MB_YESNO | MB_ICONWARNING);
        if (ret != IDYES) return;
    }

    // Prompt for unknown packs
    for (size_t i = 0; i < MainWindow::g_entries.size(); ++i)
    {
        if (MainWindow::g_entries[i].type == PACK_UNKNOWN)
        {
            PackType chosen = PromptUnknownPack(hWnd, MainWindow::g_entries[i].name);
            if (chosen != PACK_UNKNOWN)
            {
                MainWindow::g_entries[i].type = chosen;
                ListViewUpdateType((int)i, chosen);
            }
            else
            {
                // Status: Skipped
                MainWindow::g_entries[i].status = 2; // Treat as completed but skipped
                ListViewUpdateStatus((int)i, 2);
                MainWindow::g_entries[i].type = PACK_UNKNOWN;
            }
        }
    }

    MainWindow::g_installing = true;
    MainWindow::g_cancelRequested = false;
    SetControlsEnabled(false);

    SetWindowTextW(s_hInstall, L"\u30AD\u30E3\u30F3\u30BB\u30EB"); // "キャンセル"

    SendMessage(s_hProg, PBM_SETRANGE, 0,
                MAKELPARAM(0, (int)MainWindow::g_entries.size()));
    SendMessage(s_hProg, PBM_SETPOS, 0, 0);

    SendMessage(s_hProgDetail, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(s_hProgDetail, PBM_SETPOS, 0, 0);
    SetWindowTextW(s_hStatusDetail, L"");

    for (int i = 0; i < (int)MainWindow::g_entries.size(); ++i)
    {
        MainWindow::g_entries[i].status = 0;
        ListViewUpdateStatus(i, 0);
    }

    InstallParams* p = new InstallParams;
    p->hWnd              = hWnd;
    p->entries           = MainWindow::g_entries;
    p->sevenZipPath      = Settings::GetSevenZipPath();
    p->resourcePacksPath = Settings::resourcePacksPath;
    p->behaviorPacksPath = Settings::behaviorPacksPath;
    p->skinPacksPath     = Settings::skinPacksPath;
    p->worldsPath        = Settings::worldsPath;

    HANDLE hThread = CreateThread(
        NULL, 0,
        PackInstaller::InstallThread,
        reinterpret_cast<LPVOID>(p),
        0, NULL);

    if (hThread)
        CloseHandle(hThread);
    else
    {
        delete p;
        MainWindow::g_installing = false;
        SetControlsEnabled(true);
    }
}

// ===================================================================
// Create Menu
// ===================================================================
static HMENU CreateAppMenu()
{
    HMENU hMenuBar = CreateMenu();
    HMENU hFile    = CreatePopupMenu();
    HMENU hOpt     = CreatePopupMenu();
    HMENU hHelp    = CreatePopupMenu();

    AppendMenuW(hFile, MF_STRING, IDM_FILE_ADD,
        L"\u30D5\u30A1\u30A4\u30EB\u3092\u8FFD\u52A0(&A)...");
    AppendMenuW(hFile, MF_STRING, IDM_FILE_CLEAR,
        L"\u30EA\u30B9\u30C8\u3092\u30AF\u30EA\u30A2(&C)");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT,
        L"\u7D42\u4E86(&X)");

    AppendMenuW(hOpt, MF_STRING, IDM_OPT_SETTINGS,
        L"\u8A2D\u5B9A(&S)...");

    AppendMenuW(hHelp, MF_STRING, IDM_HELP_ABOUT,
        L"\u30D0\u30FC\u30B8\u30E7\u30F3\u60C5\u5831(&A)...");

    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFile),
        L"\u30D5\u30A1\u30A4\u30EB(&F)");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hOpt),
        L"\u30AA\u30D7\u30B7\u30E7\u30F3(&O)");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hHelp),
        L"\u30D8\u30EB\u30D7(&H)");

    return hMenuBar;
}

// ===================================================================
// WM_CREATE
// ===================================================================
static bool OnCreate(HWND hWnd)
{
    HFONT hFont = GetModernFont();

    s_hList = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
        LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        5, 5, 482, 182,
        hWnd, reinterpret_cast<HMENU>(IDC_LIST_PACKS),
        MainWindow::g_hInst, NULL);

    if (!s_hList) return false;

    SendMessage(s_hList, WM_SETFONT, (WPARAM)hFont, TRUE);

    ListView_SetExtendedListViewStyle(s_hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {0};
    col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt     = LVCFMT_LEFT;

    col.pszText = const_cast<wchar_t*>(
        L"\u30D5\u30A1\u30A4\u30EB\u540D");
    col.cx = 248;
    ListView_InsertColumn(s_hList, 0, &col);

    col.pszText = const_cast<wchar_t*>(L"\u7A2E\u5225");
    col.cx = 138;
    ListView_InsertColumn(s_hList, 1, &col);

    col.pszText = const_cast<wchar_t*>(L"\u72B6\u614B");
    col.cx = 82;
    ListView_InsertColumn(s_hList, 2, &col);

    s_hEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"\u30D1\u30B9\u3092\u5165\u529B...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        5, 194, 272, 22,
        hWnd, reinterpret_cast<HMENU>(IDC_EDIT_PATH),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    s_hBrowse = CreateWindowExW(0,
        L"BUTTON",
        L"\u53C2\u7167...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        282, 194, 68, 22,
        hWnd, reinterpret_cast<HMENU>(IDC_BTN_BROWSE),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);

    s_hAddPath = CreateWindowExW(0,
        L"BUTTON",
        L"\u8FFD\u52A0",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        355, 194, 132, 22,
        hWnd, reinterpret_cast<HMENU>(IDC_BTN_ADD_PATH),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hAddPath, WM_SETFONT, (WPARAM)hFont, TRUE);

    s_hRemove = CreateWindowExW(0,
        L"BUTTON",
        L"\u524A\u9664",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        5, 223, 72, 24,
        hWnd, reinterpret_cast<HMENU>(IDC_BTN_REMOVE),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hRemove, WM_SETFONT, (WPARAM)hFont, TRUE);

    s_hClear = CreateWindowExW(0,
        L"BUTTON",
        L"\u5168\u524A\u9664",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        82, 223, 72, 24,
        hWnd, reinterpret_cast<HMENU>(IDC_BTN_CLEAR_ALL),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hClear, WM_SETFONT, (WPARAM)hFont, TRUE);

    s_hInstall = CreateWindowExW(0,
        L"BUTTON",
        L"\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        320, 219, 167, 30,
        hWnd, reinterpret_cast<HMENU>(IDC_BTN_INSTALL),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hInstall, WM_SETFONT, (WPARAM)hFont, TRUE);

    s_hProg = CreateWindowExW(0,
        PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        5, 260, 482, 16,
        hWnd, reinterpret_cast<HMENU>(IDC_PROGRESS),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(s_hProg, PBM_SETPOS,   0, 0);

    s_hStatus = CreateWindowExW(0,
        L"STATIC",
        L"\u5F85\u6A5F\u4E2D",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        5, 280, 482, 16,
        hWnd, reinterpret_cast<HMENU>(IDC_STATIC_STATUS),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Detail Progress Bar
    s_hProgDetail = CreateWindowExW(0,
        PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        5, 305, 482, 12,
        hWnd, reinterpret_cast<HMENU>(IDC_PROGRESS_DETAIL),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hProgDetail, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(s_hProgDetail, PBM_SETPOS,   0, 0);

    // Detail Status Text
    s_hStatusDetail = CreateWindowExW(0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        5, 322, 482, 16,
        hWnd, reinterpret_cast<HMENU>(IDC_STATIC_DETAIL),
        MainWindow::g_hInst, NULL);
    SendMessage(s_hStatusDetail, WM_SETFONT, (WPARAM)hFont, TRUE);

    DragAcceptFiles(hWnd, TRUE);

    return true;
}

// ===================================================================
// WM_DROPFILES
// ===================================================================
static void OnDropFiles(HWND hWnd, HDROP hDrop)
{
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < count; ++i)
    {
        wchar_t szFile[MAX_PATH] = {0};
        DragQueryFileW(hDrop, i, szFile, MAX_PATH);
        AddPackFile(std::wstring(szFile));
    }
    DragFinish(hDrop);
}

// ===================================================================
// WM_COMMAND
// ===================================================================
static void OnCommand(HWND hWnd, WORD id, WORD notify)
{
    switch (id)
    {
    case IDC_BTN_BROWSE:
    case IDM_FILE_ADD:
        BrowseAndAdd(hWnd);
        break;

    case IDC_BTN_ADD_PATH:
    {
        wchar_t buf[MAX_PATH] = {0};
        GetWindowTextW(s_hEdit, buf, MAX_PATH);
        if (buf[0])
        {
            AddPackFile(std::wstring(buf));
            SetWindowTextW(s_hEdit,
                L"\u30D1\u30B9\u3092\u5165\u529B...");
        }
        break;
    }

    case IDC_BTN_REMOVE:
    {
        int sel = ListView_GetNextItem(s_hList, -1, LVNI_SELECTED);
        if (sel >= 0)
        {
            ListView_DeleteItem(s_hList, sel);
            MainWindow::g_entries.erase(
                MainWindow::g_entries.begin() + sel);
        }
        break;
    }

    case IDC_BTN_CLEAR_ALL:
    case IDM_FILE_CLEAR:
        ListView_DeleteAllItems(s_hList);
        MainWindow::g_entries.clear();
        SendMessage(s_hProg, PBM_SETPOS, 0, 0);
        SetWindowTextW(s_hStatus, L"\u5F85\u6A5F\u4E2D");
        break;

    case IDC_BTN_INSTALL:
        StartInstall(hWnd);
        break;

    case IDM_OPT_SETTINGS:
        ShowOptionsDialog(hWnd);
        break;

    case IDM_HELP_ABOUT:
        ShowAboutDialog(hWnd);
        break;

    case IDM_FILE_EXIT:
        PostMessage(hWnd, WM_CLOSE, 0, 0);
        break;
    }
}

// ===================================================================
// WM_NOTIFY
// ===================================================================
static void OnNotify(HWND hWnd, LPARAM lParam)
{
    LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);

    if (pnmh->idFrom == IDC_LIST_PACKS && (pnmh->code == NM_RCLICK || pnmh->code == NM_CLICK))
    {
        LPNMITEMACTIVATE pnmia = reinterpret_cast<LPNMITEMACTIVATE>(lParam);
        if (pnmia->iItem >= 0 && pnmia->iItem < (int)MainWindow::g_entries.size() && pnmia->iSubItem == 1)
        {
            HMENU hPopup = CreatePopupMenu();
            AppendMenuW(hPopup, MF_STRING, 1001, L"\u30EA\u30BD\u30FC\u30B9\u30D1\u30C3\u30AF");
            AppendMenuW(hPopup, MF_STRING, 1002, L"\u30D3\u30D8\u30A4\u30D3\u30A2\u30FC\u30D1\u30C3\u30AF");
            AppendMenuW(hPopup, MF_STRING, 1003, L"\u30A2\u30C9\u30AA\u30F3");
            AppendMenuW(hPopup, MF_STRING, 1004, L"\u30B9\u30AD\u30F3\u30D1\u30C3\u30AF");
            AppendMenuW(hPopup, MF_STRING, 1006, L"\u30EF\u30FC\u30EB\u30C9");
            AppendMenuW(hPopup, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hPopup, MF_STRING, 1005, L"\u4E0D\u660E (\u30B9\u30AD\u30C3\u30D7)");

            POINT pt;
            GetCursorPos(&pt);
            int cmd = TrackPopupMenu(hPopup, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hPopup);

            if ((cmd >= 1001 && cmd <= 1005) || cmd == 1006)
            {
                PackType t = PACK_UNKNOWN;
                if (cmd == 1001) t = PACK_RESOURCE;
                else if (cmd == 1002) t = PACK_BEHAVIOR;
                else if (cmd == 1003) t = PACK_ADDON;
                else if (cmd == 1004) t = PACK_SKIN;
                else if (cmd == 1006) t = PACK_WORLD;

                MainWindow::g_entries[pnmia->iItem].type = t;
                ListViewUpdateType(pnmia->iItem, t);
            }
        }
    }
}

// ===================================================================
// Install Progress Callbacks
// ===================================================================
static void OnInstallProgress(WPARAM current, LPARAM total)
{
    int cur = static_cast<int>(current) + 1;
    int tot = static_cast<int>(total);
    if (tot <= 0) tot = 1;

    SendMessage(s_hProg, PBM_SETRANGE, 0, MAKELPARAM(0, tot));
    SendMessage(s_hProg, PBM_SETPOS, cur, 0);
}

static void OnInstallDetailProgress(WPARAM pct)
{
    SendMessage(s_hProgDetail, PBM_SETPOS, (int)pct, 0);
}

static void OnInstallStatus(LPARAM lParam)
{
    wchar_t* msg = reinterpret_cast<wchar_t*>(lParam);
    SetWindowTextW(s_hStatus, msg);
    delete[] msg;
}

static void OnInstallDetailStatus(LPARAM lParam)
{
    wchar_t* msg = reinterpret_cast<wchar_t*>(lParam);
    SetWindowTextW(s_hStatusDetail, msg);
    delete[] msg;
}

static void OnInstallItemUpdate(WPARAM idx, LPARAM status)
{
    int i = static_cast<int>(idx);
    if (i >= 0 && i < static_cast<int>(MainWindow::g_entries.size()))
    {
        MainWindow::g_entries[i].status = static_cast<int>(status);
        ListViewUpdateStatus(i, static_cast<int>(status));
    }
}

static void OnInstallDone(HWND hWnd, WPARAM success, LPARAM failed)
{
    MainWindow::g_installing = false;
    SetControlsEnabled(true);
    SetWindowTextW(s_hInstall, L"\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB"); // "インストール"
    EnableWindow(s_hInstall, TRUE);

    int s = static_cast<int>(success);
    int f = static_cast<int>(failed);

    if (MainWindow::g_cancelRequested)
    {
        SetWindowTextW(s_hStatus, L"\u30AD\u30E3\u30F3\u30BB\u30EB\u3055\u308C\u307E\u3057\u305F");
        MessageBoxW(hWnd, L"\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u304C\u30AD\u30E3\u30F3\u30BB\u30EB\u3055\u308C\u307E\u3057\u305F\u3002",
            L"MCPACK Injector", MB_OK | MB_ICONWARNING);
    }
    else
    {
        wchar_t msg[256] = {0};
        wsprintfW(msg, L"%d \u4EF6\u5B8C\u4E86\u3001%d \u4EF6\u30A8\u30E9\u30FC", s, f);
        SetWindowTextW(s_hStatus, msg);
        MessageBoxW(hWnd, msg, L"\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u5B8C\u4E86",
            f > 0 ? MB_OK | MB_ICONWARNING : MB_OK | MB_ICONINFORMATION);
    }
}

// ===================================================================
// Window Proc
// ===================================================================
LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        if (!OnCreate(hWnd))
            return -1;
        return 0;

    case WM_COMMAND:
        if (!g_installing)
            OnCommand(hWnd, LOWORD(wParam), HIWORD(wParam));
        return 0;

    case WM_DROPFILES:
        if (!g_installing)
            OnDropFiles(hWnd, reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_NOTIFY:
        if (!g_installing)
            OnNotify(hWnd, lParam);
        return 0;

    case WM_INSTALL_PROGRESS:
        OnInstallProgress(wParam, lParam);
        return 0;

    case WM_INSTALL_DETAIL_PROG:
        OnInstallDetailProgress(wParam);
        return 0;

    case WM_INSTALL_STATUS:
        OnInstallStatus(lParam);
        return 0;

    case WM_INSTALL_DETAIL_STATUS:
        OnInstallDetailStatus(lParam);
        return 0;

    case WM_INSTALL_ITEM_UPD:
        OnInstallItemUpdate(wParam, lParam);
        return 0;

    case WM_INSTALL_DONE:
        OnInstallDone(hWnd, wParam, lParam);
        return 0;

    case WM_INSTALL_UUID_COLLISION:
        return PromptUuidCollision(hWnd, reinterpret_cast<CollisionInfo*>(lParam));

    case WM_CLOSE:
        if (g_installing)
        {
            int ret = MessageBoxW(hWnd,
                L"\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u4E2D\u3067\u3059"
                L"\u3002\u7D42\u4E86\u3057\u307E\u3059\u304B\uFF1F",
                L"MCPACK Injector",
                MB_YESNO | MB_ICONWARNING);
            if (ret != IDYES) return 0;
        }
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ===================================================================
// Register Class
// ===================================================================
bool MainWindow::RegisterMainClass(HINSTANCE hInst)
{
    g_hInst = hInst;

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = WNDCLASS_MAIN;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    return RegisterClassExW(&wc) != 0;
}

// ===================================================================
// Create Window
// ===================================================================
HWND MainWindow::Create(HINSTANCE hInst, int nShow)
{
    g_hInst = hInst;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                  WS_MINIMIZEBOX | WS_BORDER;

    RECT rc = {0, 0, CLIENT_W, CLIENT_H};
    AdjustWindowRectEx(&rc, style, TRUE, 0);

    int winW = rc.right  - rc.left;
    int winH = rc.bottom - rc.top;

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (scrW - winW) / 2;
    int posY = (scrH - winH) / 2;
    if (posX < 0) posX = 0;
    if (posY < 0) posY = 0;

    HMENU hMenu = CreateAppMenu();

    HWND hWnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        WNDCLASS_MAIN,
        L"MCPACK Injector ver0.0.1x144",
        style,
        posX, posY, winW, winH,
        NULL, hMenu, hInst, NULL);

    if (!hWnd)
    {
        DestroyMenu(hMenu);
        return NULL;
    }

    g_hWnd = hWnd;

    ShowWindow(hWnd, nShow);
    UpdateWindow(hWnd);

    return hWnd;
}
