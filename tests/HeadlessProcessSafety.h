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

inline void suppressWindowsErrorDialogs() noexcept
{
    SetErrorMode(GetErrorMode() | windowsHeadlessErrorModeFlags);
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
