// Settings.h - MCPACK Injector Settings Management
#pragma once
#include <windows.h>
#include <string>

namespace Settings
{
    // Path for Resource Packs
    extern std::wstring resourcePacksPath;

    // Path for Behavior Packs
    extern std::wstring behaviorPacksPath;

    // Path for Skin Packs
    extern std::wstring skinPacksPath;

    // Path for Minecraft Worlds
    extern std::wstring worldsPath;

    // Load settings from INI file
    void Load();

    // Save settings to INI file
    void Save();

    // Returns full path to 7z.exe
    std::wstring GetSevenZipPath();
}
