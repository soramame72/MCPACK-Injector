// PackDetector.h - MCPACK Injector Pack Detection
#pragma once
#include <windows.h>
#include <string>

// =====================================================
// Pack Type Enum
// =====================================================
enum PackType
{
    PACK_UNKNOWN  = 0,
    PACK_RESOURCE = 1,
    PACK_BEHAVIOR = 2,
    PACK_ADDON    = 3,
    PACK_SKIN     = 4,
};

namespace PackDetector
{
    // Detect pack type from archive file
    PackType Detect(const std::wstring& archivePath,
                    const std::wstring& sevenZipPath);

    // Detect pack type from extracted manifest
    PackType DetectFromManifest(const std::wstring& manifestFilePath);

    // Get display name (returns UCN string for UI)
    const wchar_t* GetTypeName(PackType type);
}
