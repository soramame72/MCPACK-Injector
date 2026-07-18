// PackInstaller.cpp - MCPACK Injector Install Thread Implementation
#include "PackInstaller.h"
#include "PackDetector.h"
#include "MainWindow.h" // For g_cancelRequested
#include <shlwapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <objbase.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

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

// -------------------------------------------------------
// UUID Generation
// -------------------------------------------------------
static std::wstring GenerateNewUUID()
{
    GUID guid;
    if (CoCreateGuid(&guid) == S_OK)
    {
        wchar_t szUuid[40] = {0};
        wsprintfW(szUuid, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1],
            guid.Data4[2], guid.Data4[3], guid.Data4[4],
            guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        return std::wstring(szUuid);
    }
    return L"";
}

// -------------------------------------------------------
// Replace UUID in manifest.json
// -------------------------------------------------------
static bool ReplaceUUIDInFile(const std::wstring& filePath, const std::wstring& oldUuid, const std::wstring& newUuid)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(hFile, NULL);
    if (size == 0 || size == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return false;
    }
    std::vector<char> buf(size);
    DWORD read = 0;
    ReadFile(hFile, &buf[0], size, &read, NULL);
    CloseHandle(hFile);

    std::string content(&buf[0], read);
    std::string oStr(oldUuid.begin(), oldUuid.end());
    std::string nStr(newUuid.begin(), newUuid.end());

    std::string contentLower = content;
    std::transform(contentLower.begin(), contentLower.end(), contentLower.begin(), ::tolower);
    std::string oStrLower = oStr;
    std::transform(oStrLower.begin(), oStrLower.end(), oStrLower.begin(), ::tolower);

    size_t pos = 0;
    bool modified = false;
    while (true)
    {
        pos = contentLower.find(oStrLower, pos);
        if (pos == std::string::npos) break;
        content.replace(pos, oStr.size(), nStr);
        contentLower.replace(pos, oStr.size(), nStr);
        modified = true;
        pos += nStr.size();
    }

    if (modified)
    {
        hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        WriteFile(hFile, content.c_str(), (DWORD)content.size(), &written, NULL);
        CloseHandle(hFile);
    }
    return true;
}

// -------------------------------------------------------
// Find conflict by UUID
// -------------------------------------------------------
static std::wstring FindExistingPackByUUID(const std::wstring& parentDir, const std::wstring& targetUuid)
{
    if (targetUuid.empty()) return L"";
    std::wstring pattern = parentDir + L"\\*";
    WIN32_FIND_DATAW fd = {0};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return L"";

    std::wstring result = L"";
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring subDir = parentDir + L"\\" + fd.cFileName;
        std::wstring manifestPath = subDir + L"\\manifest.json";
        if (PathFileExistsW(manifestPath.c_str()))
        {
            std::wstring uuid = PackDetector::ExtractUUID(manifestPath);
            if (_wcsicmp(uuid.c_str(), targetUuid.c_str()) == 0)
            {
                result = subDir;
                break;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return result;
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
// Install .mcpack / .zip / .mcworld
// =======================================================
static bool InstallSinglePack(const PackEntry&      entry,
                               const std::wstring&   sevenZipPath,
                               const std::wstring&   resPath,
                               const std::wstring&   behPath,
                               const std::wstring&   skinPath,
                               const std::wstring&   worldPath,
                               HWND                  hWnd,
                               std::vector<std::wstring>& rollbackList)
{
    wchar_t packName[MAX_PATH] = {0};
    wcscpy_s(packName, _countof(packName), entry.name.c_str());
    PathRemoveExtensionW(packName);

    std::wstring destBase;
    if (entry.type == PACK_BEHAVIOR) destBase = behPath;
    else if (entry.type == PACK_SKIN) destBase = skinPath;
    else if (entry.type == PACK_WORLD) destBase = worldPath;
    else destBase = resPath;

    // Temporary extraction directory to perform conflict check
    std::wstring tempDir = MakeTempDirInst();
    if (!Run7zFull(sevenZipPath, entry.path, tempDir, hWnd))
    {
        RemoveDirInst(tempDir);
        return false;
    }

    std::wstring finalPackName = packName;
    std::wstring targetDir = destBase + L"\\" + finalPackName;

    // Perform UUID Conflict Check
    if (entry.type != PACK_WORLD)
    {
        std::wstring manifestPath = tempDir + L"\\manifest.json";
        if (PathFileExistsW(manifestPath.c_str()))
        {
            std::wstring uuid = PackDetector::ExtractUUID(manifestPath);
            std::wstring existing = FindExistingPackByUUID(destBase, uuid);
            if (!existing.empty())
            {
                CollisionInfo ci;
                ci.fileName = entry.name.c_str();
                ci.existingPath = existing.c_str();
                LRESULT res = SendMessage(hWnd, WM_INSTALL_UUID_COLLISION, 0, reinterpret_cast<LPARAM>(&ci));
                if (res == 1) // Overwrite
                {
                    RemoveDirInst(existing);
                }
                else if (res == 2) // Skip
                {
                    RemoveDirInst(tempDir);
                    return false;
                }
                else if (res == 3) // Keep both (Rename and Change UUID)
                {
                    std::wstring newUuid = GenerateNewUUID();
                    if (!newUuid.empty())
                    {
                        ReplaceUUIDInFile(manifestPath, uuid, newUuid);
                    }
                    // Find a unique name
                    int suffix = 1;
                    do {
                        wchar_t tmp[MAX_PATH];
                        wsprintfW(tmp, L"%s_%d", packName, suffix++);
                        finalPackName = tmp;
                        targetDir = destBase + L"\\" + finalPackName;
                    } while (PathFileExistsW(targetDir.c_str()));
                }
            }
        }
    }
    else // World folder name conflict check
    {
        if (PathFileExistsW(targetDir.c_str()))
        {
            CollisionInfo ci;
            ci.fileName = entry.name.c_str();
            ci.existingPath = targetDir.c_str();
            LRESULT res = SendMessage(hWnd, WM_INSTALL_UUID_COLLISION, 0, reinterpret_cast<LPARAM>(&ci));
            if (res == 1) // Overwrite
            {
                RemoveDirInst(targetDir);
            }
            else if (res == 2) // Skip
            {
                RemoveDirInst(tempDir);
                return false;
            }
            else if (res == 3) // Keep both (Rename)
            {
                int suffix = 1;
                do {
                    wchar_t tmp[MAX_PATH];
                    wsprintfW(tmp, L"%s_%d", packName, suffix++);
                    finalPackName = tmp;
                    targetDir = destBase + L"\\" + finalPackName;
                } while (PathFileExistsW(targetDir.c_str()));
            }
        }
    }

    rollbackList.push_back(targetDir);
    bool ok = CopyFolderInto(tempDir, targetDir);
    RemoveDirInst(tempDir);
    return ok;
}

// =======================================================
// Install .mcaddon
// =======================================================
static bool InstallAddon(const PackEntry&      entry,
                          const std::wstring&   sevenZipPath,
                          const std::wstring&   resPath,
                          const std::wstring&   behPath,
                          HWND                  hWnd,
                          std::vector<std::wstring>& rollbackList)
{
    wchar_t packName[MAX_PATH] = {0};
    wcscpy_s(packName, _countof(packName), entry.name.c_str());
    PathRemoveExtensionW(packName);
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
        std::wstring target = dest + L"\\" + packName;

        // Perform UUID Conflict Check
        std::wstring uuid = PackDetector::ExtractUUID(std::wstring(rootManifest));
        std::wstring existing = FindExistingPackByUUID(dest, uuid);
        bool skip = false;
        if (!existing.empty())
        {
            CollisionInfo ci;
            ci.fileName = entry.name.c_str();
            ci.existingPath = existing.c_str();
            LRESULT res = SendMessage(hWnd, WM_INSTALL_UUID_COLLISION, 0, reinterpret_cast<LPARAM>(&ci));
            if (res == 1) // Overwrite
            {
                RemoveDirInst(existing);
            }
            else if (res == 2) // Skip
            {
                skip = true;
            }
            else if (res == 3) // Keep both
            {
                std::wstring newUuid = GenerateNewUUID();
                if (!newUuid.empty())
                {
                    ReplaceUUIDInFile(std::wstring(rootManifest), uuid, newUuid);
                }
                int suffix = 1;
                do {
                    wchar_t tmp[MAX_PATH];
                    wsprintfW(tmp, L"%s_%d", packName, suffix++);
                    target = dest + L"\\" + tmp;
                } while (PathFileExistsW(target.c_str()));
            }
        }

        if (!skip)
        {
            rollbackList.push_back(target);
            anyOk = CopyFolderInto(tempDir, target);
        }
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

            const wchar_t* subName = PathFindFileNameW(subs[i].c_str());
            std::wstring target = dest + L"\\" + packName + L"_" + subName;

            // Perform UUID Conflict Check
            std::wstring uuid = PackDetector::ExtractUUID(std::wstring(mf));
            std::wstring existing = FindExistingPackByUUID(dest, uuid);
            bool skip = false;
            if (!existing.empty())
            {
                CollisionInfo ci;
                ci.fileName = entry.name.c_str();
                ci.existingPath = existing.c_str();
                LRESULT res = SendMessage(hWnd, WM_INSTALL_UUID_COLLISION, 0, reinterpret_cast<LPARAM>(&ci));
                if (res == 1) // Overwrite
                {
                    RemoveDirInst(existing);
                }
                else if (res == 2) // Skip
                {
                    skip = true;
                }
                else if (res == 3) // Keep both
                {
                    std::wstring newUuid = GenerateNewUUID();
                    if (!newUuid.empty())
                    {
                        ReplaceUUIDInFile(std::wstring(mf), uuid, newUuid);
                    }
                    int suffix = 1;
                    do {
                        wchar_t tmp[MAX_PATH];
                        wsprintfW(tmp, L"%s_%s_%d", packName, subName, suffix++);
                        target = dest + L"\\" + tmp;
                    } while (PathFileExistsW(target.c_str()));
                }
            }

            if (!skip)
            {
                rollbackList.push_back(target);
                if (CopyFolderInto(subs[i], target))
                    anyOk = true;
            }
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

    std::vector<std::wstring> rollbackList;

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
                              p->hWnd,
                              rollbackList);
        }
        else
        {
            ok = InstallSinglePack(e,
                                   p->sevenZipPath,
                                   p->resourcePacksPath,
                                   p->behaviorPacksPath,
                                   p->skinPacksPath,
                                   p->worldsPath,
                                   p->hWnd,
                                   rollbackList);
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
    else
    {
        PostWStr(p->hWnd, WM_INSTALL_DETAIL_STATUS, 0, L"\u30ED\u30FC\u30EB\u30D0\u30C3\u30AF\u4E2D..."); // Rolling back...
        for (size_t i = 0; i < rollbackList.size(); ++i)
        {
            RemoveDirInst(rollbackList[i]);
        }
    }

    PostMessage(p->hWnd, WM_INSTALL_DONE,
                (WPARAM)success, (LPARAM)failed);

    delete p;
    return 0;
}
