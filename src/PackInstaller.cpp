// PackInstaller.cpp - MCPACK Injector Install Thread Implementation
#include "PackInstaller.h"
#include "PackDetector.h"
#include "MainWindow.h" // For g_cancelRequested
#include <shlwapi.h>
#include <vector>
#include <string>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// =======================================================
// Utils
// =======================================================
static void PostWStr(HWND hWnd, UINT msg, WPARAM wp, const std::wstring& str)
{
    size_t len = str.size() + 1;
    wchar_t* p = new wchar_t[len];
    wcscpy_s(p, len, str.c_str());
    PostMessage(hWnd, msg, wp, reinterpret_cast<LPARAM>(p));
}

static std::wstring MakeTempDirInst()
{
    wchar_t szTmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, szTmp);

    static LONG s_seq = 0;
    LONG n = InterlockedIncrement(&s_seq);

    wchar_t szDir[MAX_PATH] = {0};
    wsprintfW(szDir, L"%sMCPKInst_%08X_%08X", szTmp, n, GetTickCount());
    CreateDirectoryW(szDir, NULL);
    return std::wstring(szDir);
}

static void RemoveDirInst(const std::wstring& dir)
{
    std::vector<wchar_t> buf(dir.begin(), dir.end());
    buf.push_back(L'\0');
    buf.push_back(L'\0');

    SHFILEOPSTRUCTW op = {0};
    op.wFunc  = FO_DELETE;
    op.pFrom  = &buf[0];
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

static bool CopyFolderInto(const std::wstring& src, const std::wstring& destParent)
{
    std::vector<wchar_t> fromBuf(src.begin(), src.end());
    fromBuf.push_back(L'\0');
    fromBuf.push_back(L'\0');

    std::vector<wchar_t> toBuf(destParent.begin(), destParent.end());
    toBuf.push_back(L'\0');
    toBuf.push_back(L'\0');

    SHFILEOPSTRUCTW op = {0};
    op.wFunc  = FO_COPY;
    op.pFrom  = &fromBuf[0];
    op.pTo    = &toBuf[0];
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return (SHFileOperationW(&op) == 0);
}

// =======================================================
// Run 7z.exe extract all
// =======================================================
static bool Run7zFull(const std::wstring& sevenZipPath,
                       const std::wstring& archivePath,
                       const std::wstring& destDir,
                       HWND hNotifyWnd)
{
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 8192))
        return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    // Use -bsp1 to enable standard progress output
    std::wstring cmd =
        L"\"" + sevenZipPath + L"\" x \"" + archivePath +
        L"\" -o\"" + destDir + L"\" -y -bsp1";

    STARTUPINFOW si = {0};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;

    PROCESS_INFORMATION pi = {0};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(NULL, &cmdBuf[0],
                        NULL, NULL, TRUE,
                        CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi))
    {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }

    CloseHandle(hWrite);

    char buf[512];
    DWORD bytesRead = 0;
    std::string textBuffer;

    while (true)
    {
        if (MainWindow::g_cancelRequested)
        {
            TerminateProcess(pi.hProcess, 1);
            break;
        }

        // Use PeekNamedPipe to avoid blocking indefinitely if 7z is busy
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &bytesAvailable, NULL))
        {
            if (bytesAvailable > 0)
            {
                if (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0)
                {
                    buf[bytesRead] = '\0';
                    textBuffer += buf;

                    // Parse progress e.g. " 12%"
                    size_t pctPos = textBuffer.rfind('%');
                    if (pctPos != std::string::npos && pctPos > 0)
                    {
                        size_t startPos = pctPos - 1;
                        while (startPos > 0 && isdigit((unsigned char)textBuffer[startPos]))
                            startPos--;
                        if (!isdigit((unsigned char)textBuffer[startPos]))
                            startPos++;

                        std::string numStr = textBuffer.substr(startPos, pctPos - startPos);
                        int pct = atoi(numStr.c_str());
                        if (pct >= 0 && pct <= 100)
                        {
                            PostMessage(hNotifyWnd, WM_INSTALL_DETAIL_PROG, (WPARAM)pct, 0);
                        }
                    }

                    // Keep buffer small
                    if (textBuffer.size() > 1024)
                        textBuffer = textBuffer.substr(textBuffer.size() - 512);
                }
            }
            else
            {
                // No data available, check if process exited
                if (WaitForSingleObject(pi.hProcess, 10) != WAIT_TIMEOUT)
                    break;
            }
        }
        else
        {
            break; // Pipe closed
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    return (exitCode == 0 && !MainWindow::g_cancelRequested);
}

// =======================================================
// Find addon subfolders
// =======================================================
static std::vector<std::wstring> FindPackSubFolders(const std::wstring& parentDir)
{
    std::vector<std::wstring> result;

    std::wstring pattern = parentDir + L"\\*";
    WIN32_FIND_DATAW fd = {0};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return result;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        wchar_t subDir[MAX_PATH] = {0};
        wcscpy_s(subDir, _countof(subDir), parentDir.c_str());
        PathAppendW(subDir, fd.cFileName);

        wchar_t manifest[MAX_PATH] = {0};
        wcscpy_s(manifest, _countof(manifest), subDir);
        PathAppendW(manifest, L"manifest.json");

        if (PathFileExistsW(manifest))
            result.push_back(std::wstring(subDir));

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return result;
}

// =======================================================
// Install .mcpack / .zip
// =======================================================
static bool InstallSinglePack(const PackEntry&      entry,
                               const std::wstring&   sevenZipPath,
                               const std::wstring&   resPath,
                               const std::wstring&   behPath,
                               HWND                  hWnd)
{
    wchar_t packName[MAX_PATH] = {0};
    wcscpy_s(packName, _countof(packName), entry.name.c_str());
    PathRemoveExtensionW(packName);

    const std::wstring& destBase =
        (entry.type == PACK_BEHAVIOR) ? behPath : resPath;

    std::wstring destDir = destBase + L"\\" + packName;

    return Run7zFull(sevenZipPath, entry.path, destDir, hWnd);
}

// =======================================================
// Install .mcaddon
// =======================================================
static bool InstallAddon(const PackEntry&      entry,
                          const std::wstring&   sevenZipPath,
                          const std::wstring&   resPath,
                          const std::wstring&   behPath,
                          HWND                  hWnd)
{
    std::wstring tempDir = MakeTempDirInst();
    bool anyOk = false;

    if (!Run7zFull(sevenZipPath, entry.path, tempDir, hWnd))
    {
        RemoveDirInst(tempDir);
        return false;
    }

    wchar_t rootManifest[MAX_PATH] = {0};
    wcscpy_s(rootManifest, _countof(rootManifest), tempDir.c_str());
    PathAppendW(rootManifest, L"manifest.json");

    std::vector<std::wstring> subs = FindPackSubFolders(tempDir);

    if (subs.empty() && PathFileExistsW(rootManifest))
    {
        PackType t = PackDetector::DetectFromManifest(std::wstring(rootManifest));
        const std::wstring& dest = (t == PACK_BEHAVIOR) ? behPath : resPath;
        anyOk = CopyFolderInto(tempDir, dest);
    }
    else
    {
        for (size_t i = 0; i < subs.size(); ++i)
        {
            wchar_t mf[MAX_PATH] = {0};
            wcscpy_s(mf, _countof(mf), subs[i].c_str());
            PathAppendW(mf, L"manifest.json");

            PackType t = PackDetector::DetectFromManifest(std::wstring(mf));
            const std::wstring& dest = (t == PACK_BEHAVIOR) ? behPath : resPath;

            if (CopyFolderInto(subs[i], dest))
                anyOk = true;
        }
    }

    RemoveDirInst(tempDir);
    return anyOk;
}

// =======================================================
// Install Thread
// =======================================================
DWORD WINAPI PackInstaller::InstallThread(LPVOID param)
{
    InstallParams* p = reinterpret_cast<InstallParams*>(param);

    int total   = static_cast<int>(p->entries.size());
    int success = 0;
    int failed  = 0;

    for (int i = 0; i < total; ++i)
    {
        if (MainWindow::g_cancelRequested)
            break;

        PackEntry& e = p->entries[i];

        if (e.type == PACK_UNKNOWN || e.status == 2)
        {
            // Skipped by user or undetected
            continue;
        }

        std::wstring statusMsg =
            std::wstring(L"\u5C55\u958B\u4E2D: ") + e.name;
        PostWStr(p->hWnd, WM_INSTALL_STATUS, 0, statusMsg);
        PostMessage(p->hWnd, WM_INSTALL_DETAIL_PROG, 0, 0);

        e.status = 1;
        PostMessage(p->hWnd, WM_INSTALL_ITEM_UPD, (WPARAM)i, (LPARAM)1);
        PostMessage(p->hWnd, WM_INSTALL_PROGRESS, (WPARAM)i, (LPARAM)total);

        std::wstring detailMsg = L"7-Zip \u3067\u5C55\u958B\u4E2D...";
        PostWStr(p->hWnd, WM_INSTALL_DETAIL_STATUS, 0, detailMsg);

        bool ok = false;

        if (e.type == PACK_ADDON)
        {
            ok = InstallAddon(e,
                              p->sevenZipPath,
                              p->resourcePacksPath,
                              p->behaviorPacksPath,
                              p->hWnd);
        }
        else
        {
            ok = InstallSinglePack(e,
                                   p->sevenZipPath,
                                   p->resourcePacksPath,
                                   p->behaviorPacksPath,
                                   p->hWnd);
        }

        if (ok && !MainWindow::g_cancelRequested)
        {
            ++success;
            e.status = 2;
        }
        else
        {
            ++failed;
            e.status = 3;
        }
        PostMessage(p->hWnd, WM_INSTALL_ITEM_UPD, (WPARAM)i, (LPARAM)e.status);
    }

    if (!MainWindow::g_cancelRequested)
    {
        PostMessage(p->hWnd, WM_INSTALL_PROGRESS, (WPARAM)total, (LPARAM)total);
        PostMessage(p->hWnd, WM_INSTALL_DETAIL_PROG, 100, 0);

        std::wstring doneMsg = L"\u5B8C\u4E86";
        PostWStr(p->hWnd, WM_INSTALL_STATUS, 0, doneMsg);
        PostWStr(p->hWnd, WM_INSTALL_DETAIL_STATUS, 0, L"");
    }

    PostMessage(p->hWnd, WM_INSTALL_DONE,
                (WPARAM)success, (LPARAM)failed);

    delete p;
    return 0;
}
