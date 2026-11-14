/*
 * dllmain.cpp - Sets the USF decoder component's file name and version.
 *               Defines the entry point for the DLL application.
 *
 * Copyright (c) 2023-2026 Jimmy Cassis
 */

#include "pch.h"
#include "version.h"

#pragma hdrstop

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(lpReserved);

    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
        default:
            break;
    }

    return TRUE;
}

#include "library.h"

// clang-format off
#define APPLICATION_SOURCE_URLS \
    APPLICATION_FILE_NAME " component source code can be obtained from:\n" \
    " * https://www.foobar2000.org/SDK [" STR(FOOBAR2000_SDK_VERSION) "] (external SDK)\n" \
    " * n64emu USF + PSF/miniz loader [" N64EMU_USF_VERSION "] (BSD 3-Clause License)\n" \
    " * https://wtl.sourceforge.io/ [" WTL_VERSION "] (Microsoft Public License)\n" \
    " * https://learn.microsoft.com/cpp/atl/ [" _ATL_VER_RBLD "] (Microsoft Visual Studio License)\n" \
    " * https://learn.microsoft.com/windows/apps/windows-sdk/ [" WINDOWS_SDK_VERSION "] (Microsoft Windows SDK License)"

DECLARE_COMPONENT_VERSION(APPLICATION_PRODUCT_NAME, APPLICATION_VERSION,
                          COMPONENT_DESCRIPTION "\n\n"
                          APPLICATION_FILE_NAME " " APPLICATION_VERSION "\n"
                          "Copyright (c) " APPLICATION_COPYRIGHT "\n\n"
                          "foobar2000 component by " APPLICATION_AUTHOR_NAME " using SDK " STR(FOOBAR2000_SDK_VERSION) ".\n"
                          "Built with MSVC " STR(_MSC_FULL_VER) " on " __DATE__ " " __TIME__ " " STR(BUILD_TIMEZONE) ".\n\n"
                          APPLICATION_SOURCE_URLS)
// clang-format on

VALIDATE_COMPONENT_FILENAME(APPLICATION_FILE_NAME _APPLICATION_EXTENSION)
