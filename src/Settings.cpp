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
void Settings::Load()
// -------------------------------------------------------
{
    std::wstring cfg  = GetConfigPath();
    std::wstring base = GetMojangBase();

    std::wstring defRes = base + L"\\resource_packs";
    std::wstring defBeh = base + L"\\behavior_packs";

    wchar_t buf[MAX_PATH] = {0};

    GetPrivateProfileStringW(
        L"Paths", L"ResourcePacks",
        defRes.c_str(), buf, MAX_PATH, cfg.c_str());
    Settings::resourcePacksPath = buf;

    GetPrivateProfileStringW(
        L"Paths", L"BehaviorPacks",
        defBeh.c_str(), buf, MAX_PATH, cfg.c_str());
    Settings::behaviorPacksPath = buf;
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
