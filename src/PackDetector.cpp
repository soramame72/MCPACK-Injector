// PackDetector.cpp - MCPACK Injector Pack Detection Implementation
#include "PackDetector.h"
#include <shlwapi.h>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// -------------------------------------------------------
// Make temp dir
// -------------------------------------------------------
static std::wstring MakeTempDirDetect()
{
    wchar_t szTmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, szTmp);

    static LONG s_seq = 0;
    LONG n = InterlockedIncrement(&s_seq);

    wchar_t szDir[MAX_PATH] = {0};
    wsprintfW(szDir, L"%sMCPKDet_%08X_%08X", szTmp, n, GetCurrentThreadId());
    CreateDirectoryW(szDir, NULL);
    return std::wstring(szDir);
}

// -------------------------------------------------------
// Remove dir
// -------------------------------------------------------
static void RemoveDir(const std::wstring& dir)
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

// -------------------------------------------------------
// Run 7z.exe to extract a specific file
// -------------------------------------------------------
static bool Run7zExtractFile(const std::wstring& sevenZipPath,
                              const std::wstring& archivePath,
                              const std::wstring& fileInArch,
                              const std::wstring& destDir)
{
    std::wstring cmd =
        L"\"" + sevenZipPath + L"\" e \"" + archivePath +
        L"\" \"" + fileInArch + L"\" -o\"" + destDir + L"\" -y";

    STARTUPINFOW si = {0};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_HIDE;

    PROCESS_INFORMATION pi = {0};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(NULL, &cmdBuf[0],
                        NULL, NULL, FALSE,
                        CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi))
    {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 10000);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

// -------------------------------------------------------
// Read manifest.json
// -------------------------------------------------------
PackType PackDetector::DetectFromManifest(const std::wstring& manifestFilePath)
{
    HANDLE hFile = CreateFileW(
        manifestFilePath.c_str(),
        GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return PACK_UNKNOWN;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0 || fileSize > 65535)
    {
        CloseHandle(hFile);
        return PACK_UNKNOWN;
    }

    std::vector<char> buf(fileSize + 1, '\0');
    DWORD bytesRead = 0;
    ReadFile(hFile, &buf[0], fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    std::string content(&buf[0], bytesRead);
    for (size_t i = 0; i < content.size(); ++i)
        content[i] = static_cast<char>(tolower(static_cast<unsigned char>(content[i])));

    if (content.find("\"resources\"") != std::string::npos)
        return PACK_RESOURCE;

    if (content.find("\"data\"") != std::string::npos)
        return PACK_BEHAVIOR;

    if (content.find("\"javascript\"") != std::string::npos)
        return PACK_BEHAVIOR;

    if (content.find("\"skin_pack\"") != std::string::npos)
        return PACK_SKIN;

    return PACK_UNKNOWN;
}

// -------------------------------------------------------
// Detect pack type from archive
// -------------------------------------------------------
PackType PackDetector::Detect(const std::wstring& archivePath,
                               const std::wstring& sevenZipPath)
{
    const wchar_t* pExt = PathFindExtensionW(archivePath.c_str());
    if (pExt && _wcsicmp(pExt, L".mcaddon") == 0)
        return PACK_ADDON;

    std::wstring tempDir = MakeTempDirDetect();
    PackType result = PACK_UNKNOWN;

    bool extracted = Run7zExtractFile(sevenZipPath, archivePath,
                                      L"manifest.json", tempDir);

    if (!extracted)
    {
        extracted = Run7zExtractFile(sevenZipPath, archivePath,
                                     L"*/manifest.json", tempDir);
    }

    if (extracted)
    {
        wchar_t szManifest[MAX_PATH] = {0};
        wcscpy_s(szManifest, _countof(szManifest), tempDir.c_str());
        PathAppendW(szManifest, L"manifest.json");

        if (PathFileExistsW(szManifest))
            result = DetectFromManifest(std::wstring(szManifest));
    }

    RemoveDir(tempDir);
    return result;
}

// -------------------------------------------------------
// Get Display Name
// -------------------------------------------------------
const wchar_t* PackDetector::GetTypeName(PackType type)
{
    switch (type)
    {
    case PACK_RESOURCE:
        return L"\u30EA\u30BD\u30FC\u30B9\u30D1\u30C3\u30AF";
    case PACK_BEHAVIOR:
        return L"\u30D3\u30D8\u30A4\u30D3\u30A2\u30FC\u30D1\u30C3\u30AF";
    case PACK_ADDON:
        return L"\u30A2\u30C9\u30AA\u30F3";
    case PACK_SKIN:
        return L"\u30B9\u30AD\u30F3\u30D1\u30C3\u30AF";
    default:
        return L"\u4E0D\u660E";
    }
}
