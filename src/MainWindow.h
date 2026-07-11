// MainWindow.h - MCPACK Injector Main Window
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include "PackInstaller.h"
#include "PackDetector.h"

namespace MainWindow
{
    extern HINSTANCE g_hInst;
    extern HWND g_hWnd;
    extern std::vector<PackEntry> g_entries;
    extern bool g_installing;

    bool RegisterMainClass(HINSTANCE hInst);
    HWND Create(HINSTANCE hInst, int nShow);
    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
}
