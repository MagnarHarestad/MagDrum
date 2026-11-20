#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <werapi.h>

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
    // SEM_NOGPFAULTERRORBOX prevents the standard unhandled-exception dialog,
    // but Windows Error Reporting has a separate per-process UI flag. Keep
    // WER reports available to Windows while explicitly disabling its UI.
    constexpr DWORD windowsWerNoUi = 0x0020;
    (void) WerSetFlags(windowsWerNoUi);
    SetUnhandledExceptionFilter(terminateHeadlessProcessOnUnhandledException);
}

inline bool windowsErrorDialogsAreSuppressed() noexcept
{
    DWORD werFlags = 0;
    return (GetErrorMode() & windowsHeadlessErrorModeFlags) == windowsHeadlessErrorModeFlags
        && SUCCEEDED(WerGetFlags(GetCurrentProcess(), &werFlags))
        && (werFlags & 0x0020) != 0;
}
}
#else
namespace magdrum::tests
{
inline void suppressWindowsErrorDialogs() noexcept {}
inline bool windowsErrorDialogsAreSuppressed() noexcept { return true; }
}
#endif
