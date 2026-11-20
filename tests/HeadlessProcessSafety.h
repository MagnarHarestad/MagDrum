#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace magdrum::tests
{
inline constexpr UINT windowsHeadlessErrorModeFlags =
    SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;

inline LONG WINAPI terminateHeadlessProcessOnUnhandledException(EXCEPTION_POINTERS*) noexcept
{
    // Keep an access violation in a test worker from opening the interactive
    // Windows crash dialog. CTest still receives the abnormal process exit and
    // reports the failure in its log.
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void suppressWindowsErrorDialogs() noexcept
{
    SetErrorMode(GetErrorMode() | windowsHeadlessErrorModeFlags);
    SetUnhandledExceptionFilter(terminateHeadlessProcessOnUnhandledException);
}

inline bool windowsErrorDialogsAreSuppressed() noexcept
{
    return (GetErrorMode() & windowsHeadlessErrorModeFlags) == windowsHeadlessErrorModeFlags;
}
}
#else
namespace magdrum::tests
{
inline void suppressWindowsErrorDialogs() noexcept {}
inline bool windowsErrorDialogsAreSuppressed() noexcept { return true; }
}
#endif
