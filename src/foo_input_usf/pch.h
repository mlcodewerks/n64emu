/*
 * pch.h - Pre-compiled header file.
 *
 * Copyright (c) 2023-2026 Jimmy Cassis
 */

#pragma once

#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#define _new_dbg new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include <winsdkver.h>
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <sdkddkver.h>

#define NOBITMAP
#define NOMINMAX
#define NOMCX
#define NOSERVICE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <helpers/foobar2000-lite+atl.h>
#include <sdk/componentversion.h>
#include <sdk/coreDarkMode.h>
#include <sdk/input_file_type.h>
#include <sdk/file_info_filter.h>
#include <sdk/file_info_impl.h>
#include <sdk/input_impl.h>
#include <helpers/atl-misc.h>
#include <helpers/window_placement_helper.h>
#include <helpers/win32_dialog.h>
#include <libppui/gdiplus_helpers.h>
#include <libppui/wtl-pp.h>

#include <usf/usf.h>


#ifdef _new_dbg
#define new _new_dbg
#endif

#define WM_USF (WM_USER + 0)
#define WM_CONFIG_CHANGE (WM_USER + 1)

#define USF_CONSOLE_PRINT(...) FB2K_console_print(core_api::get_my_file_name(), ": ", __VA_ARGS__);
#ifndef _DEBUG
#define USF_CONSOLE_LOG(...)
#define USF_CONSOLE_TRACE(...)
#define USF_CONSOLE_DEBUG(...)
#define USF_CONSOLE_INFO(...) USF_CONSOLE_PRINT(__VA_ARGS__)
#define USF_CONSOLE_WARN(...) USF_CONSOLE_PRINT(__VA_ARGS__)
#define USF_CONSOLE_ERROR(...) USF_CONSOLE_PRINT(__VA_ARGS__)
#define USF_CONSOLE_CRITICAL(...) USF_CONSOLE_PRINT(__VA_ARGS__)
#define DBG(a)
#else
#define USF_CONSOLE_LOG(...) USF_CONSOLE_PRINT(__VA_ARGS__)
#define USF_CONSOLE_TRACE(...) USF_CONSOLE_LOG(__VA_ARGS__)
#define USF_CONSOLE_DEBUG(...) USF_CONSOLE_LOG(__VA_ARGS__)
#define USF_CONSOLE_INFO(...) USF_CONSOLE_LOG(__VA_ARGS__)
#define USF_CONSOLE_WARN(...) USF_CONSOLE_LOG(__VA_ARGS__)
#define USF_CONSOLE_ERROR(...) USF_CONSOLE_LOG(__VA_ARGS__)
#define USF_CONSOLE_CRITICAL(...) USF_CONSOLE_PRINT(__VA_ARGS__)
#define DBG(a) OutputDebugStringA(a)
#endif

#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif
