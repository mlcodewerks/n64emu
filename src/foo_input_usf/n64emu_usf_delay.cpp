// Resolve the delay-loaded engine beside the foobar component, including when
// the component is installed outside the foobar executable's directory.
#include <windows.h>

#include <cstring>
#include <delayimp.h>

static FARPROC WINAPI load_n64emu_usf(unsigned event, PDelayLoadInfo info) {
    if (event != dliNotePreLoadLibrary || std::strcmp(info->szDll, "n64emu_usf.dll"))
        return nullptr;
    HMODULE component = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&load_n64emu_usf), &component))
        return nullptr;
    wchar_t path[32768];
    const DWORD count = GetModuleFileNameW(component, path, ARRAYSIZE(path));
    if (!count || count >= ARRAYSIZE(path))
        return nullptr;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || size_t(slash - path) + ARRAYSIZE(L"\\n64emu_usf.dll") > ARRAYSIZE(path))
        return nullptr;
    wcscpy_s(slash, ARRAYSIZE(path) - size_t(slash - path), L"\\n64emu_usf.dll");
    return reinterpret_cast<FARPROC>(LoadLibraryExW(
        path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = load_n64emu_usf;
