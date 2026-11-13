#ifndef N64EMU_UNIT_HEADLESS_H
#define N64EMU_UNIT_HEADLESS_H
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdlib.h>
static void unit_headless(void){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_error_mode(_OUT_TO_STDERR);}
#else
static void unit_headless(void){}
#endif
#endif
