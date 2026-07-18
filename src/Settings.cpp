// Settings.cpp - MCPACK Injector Settings Implementation
#include "Settings.h"
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace Settings
{
    std::wstring resourcePacksPath;
    std::wstring behaviorPacksPath;
    std::wstring skinPacksPath;
    std::wstring worldsPath;
}

// -------------------------------------------------------
// Get config.ini path
// -------------------------------------------------------
static std::wstring GetConfigPath()
{
    wchar_t szAppData[MAX_PATH] = {0};
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szAppData);

    wchar_t szDir[MAX_PATH] = {0};
    wcscpy_s(szDir, _countof(szDir), szAppData);
    PathAppendW(szDir, L"MCPACK_Injector");
    CreateDirectoryW(szDir, NULL);

    wchar_t szIni[MAX_PATH] = {0};
    wcscpy_s(szIni, _countof(szIni), szDir);
    PathAppendW(szIni, L"config.ini");

    return std::wstring(szIni);
}

// -------------------------------------------------------
// Get Mojang base path
// -------------------------------------------------------
static std::wstring GetMojangBase()
{
    wchar_t szAppData[MAX_PATH] = {0};
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szAppData);

    wchar_t szBase[MAX_PATH] = {0};
    wcscpy_s(szBase, _countof(szBase), szAppData);
    PathAppendW(szBase, L"Minecraft Bedrock\\Users\\Shared\\games\\com.mojang");

    return std::wstring(szBase);
}

// -------------------------------------------------------
// Get default worlds path
// Searches Minecraft Bedrock\Users\<UserID>\games\com.mojang\minecraftWorlds
// Falls back to GetMojangBase()\minecraftWorlds if not found
// -------------------------------------------------------
static std::wstring GetWorldsDefaultPath()
{
    wchar_t szAppData[MAX_PATH] = {0};
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szAppData);

    // Build search path: %AppData%\Minecraft Bedrock\Users\*
    wchar_t szSearch[MAX_PATH] = {0};
    wcscpy_s(szSearch, _countof(szSearch), szAppData);
    PathAppendW(szSearch, L"Minecraft Bedrock\\Users");

    wchar_t szPattern[MAX_PATH] = {0};
    wcscpy_s(szPattern, _countof(szPattern), szSearch);
    PathAppendW(szPattern, L"*");

    WIN32_FIND_DATAW fd = {0};
    HANDLE hFind = FindFirstFileW(szPattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            // Skip . and .. entries; look for numeric user-ID folders
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (wcscmp(fd.cFileName, L".") != 0 &&
                    wcscmp(fd.cFileName, L"..") != 0 &&
                    wcscmp(fd.cFileName, L"Shared") != 0)
                {
                    wchar_t szCandidate[MAX_PATH] = {0};
                    wcscpy_s(szCandidate, _countof(szCandidate), szSearch);
                    PathAppendW(szCandidate, fd.cFileName);
                    PathAppendW(szCandidate, L"games\\com.mojang\\minecraftWorlds");

                    FindClose(hFind);
                    return std::wstring(szCandidate);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    // Fallback
    return GetMojangBase() + L"\\minecraftWorlds";
}

// -------------------------------------------------------
void Settings::Load()
// -------------------------------------------------------
{
    std::wstring cfg  = GetConfigPath();
    std::wstring base = GetMojangBase();

    std::wstring defRes  = base + L"\\resource_packs";
    std::wstring defBeh  = base + L"\\behavior_packs";
    std::wstring defSkin = base + L"\\skin_packs";
    std::wstring defWld  = GetWorldsDefaultPath();

    wchar_t buf[MAX_PATH] = {0};

    GetPrivateProfileStringW(
        L"Paths", L"ResourcePacks",
        defRes.c_str(), buf, MAX_PATH, cfg.c_str());
    Settings::resourcePacksPath = buf;

    GetPrivateProfileStringW(
        L"Paths", L"BehaviorPacks",
        defBeh.c_str(), buf, MAX_PATH, cfg.c_str());
    Settings::behaviorPacksPath = buf;

    GetPrivateProfileStringW(
        L"Paths", L"SkinPacks",
        defSkin.c_str(), buf, MAX_PATH, cfg.c_str());
    Settings::skinPacksPath = buf;

    GetPrivateProfileStringW(
        L"Paths", L"Worlds",
        defWld.c_str(), buf, MAX_PATH, cfg.c_str());
    Settings::worldsPath = buf;
}

// -------------------------------------------------------
void Settings::Save()
// -------------------------------------------------------
{
    std::wstring cfg = GetConfigPath();

    WritePrivateProfileStringW(
        L"Paths", L"ResourcePacks",
        Settings::resourcePacksPath.c_str(), cfg.c_str());

    WritePrivateProfileStringW(
        L"Paths", L"BehaviorPacks",
        Settings::behaviorPacksPath.c_str(), cfg.c_str());

    WritePrivateProfileStringW(
        L"Paths", L"SkinPacks",
        Settings::skinPacksPath.c_str(), cfg.c_str());

    WritePrivateProfileStringW(
        L"Paths", L"Worlds",
        Settings::worldsPath.c_str(), cfg.c_str());
}

// -------------------------------------------------------
std::wstring Settings::GetSevenZipPath()
// -------------------------------------------------------
{
    wchar_t szExe[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, szExe, MAX_PATH);
    PathRemoveFileSpecW(szExe);
    PathAppendW(szExe, L"7z\\7z.exe");
    return std::wstring(szExe);
}
