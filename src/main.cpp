/**
 * ClipStash - clipboard image to PNG file
 *
 * Copyright (c) 2026 xiaohao
 * Author: xiaohao
 * GitHub: https://github.com/fengctm
 */

#include "clipstash.h"

#include <cwchar>
#include <cwctype>
#include <string>

namespace {

void ShowHelp(bool error) {
    const wchar_t* text =
        L"ClipStash - clipboard image to PNG file\n"
        L"\n"
        L"Usage: ClipStash.exe [options]\n"
        L"  --install     Register this exe in HKCU Run and start it\n"
        L"  --uninstall   Stop the running instance and remove autostart\n"
        L"  --quit        Stop the running instance\n"
        L"  --selftest    Encode test DIBs and verify PNG output\n"
        L"  --verbose     Also log successful conversions and diagnostics\n"
        L"  --help        Show this help\n"
        L"\n"
        L"Output: %TEMP%\\ClipStash\\*.png\n"
        L"Error log: %LOCALAPPDATA%\\ClipStash\\clipstash.log\n"
        L"Place the exe in a stable folder before --install.\n"
        L"Do not run ClipStash and PasteEx at the same time.";
    MessageBoxW(nullptr, text, L"ClipStash",
                MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

CliOptions ParseCli(const wchar_t* cmdLine) {
    CliOptions opt;
    if (!cmdLine || !*cmdLine) return opt;

    bool hasHelp = false;
    bool invalid = false;
    bool actionSet = false;

    auto setAction = [&](CliOptions::Action action) {
        if (actionSet) {
            invalid = true;
            return;
        }
        actionSet = true;
        opt.action = action;
    };

    const wchar_t* p = cmdLine;
    while (*p) {
        while (*p && iswspace(*p)) ++p;
        if (!*p) break;
        const wchar_t* start = p;
        while (*p && !iswspace(*p)) ++p;
        const std::wstring token(start, p);

        if (_wcsicmp(token.c_str(), L"--verbose") == 0) {
            opt.verbose = true;
        } else if (_wcsicmp(token.c_str(), L"--install") == 0) {
            setAction(CliOptions::Action::Install);
        } else if (_wcsicmp(token.c_str(), L"--uninstall") == 0) {
            setAction(CliOptions::Action::Uninstall);
        } else if (_wcsicmp(token.c_str(), L"--quit") == 0) {
            setAction(CliOptions::Action::Quit);
        } else if (_wcsicmp(token.c_str(), L"--selftest") == 0) {
            setAction(CliOptions::Action::SelfTest);
        } else if (_wcsicmp(token.c_str(), L"--help") == 0 ||
                   _wcsicmp(token.c_str(), L"-h") == 0 ||
                   _wcsicmp(token.c_str(), L"/?") == 0) {
            hasHelp = true;
        } else {
            invalid = true;
        }
    }

    if (hasHelp) {
        opt.action = CliOptions::Action::Help;
    } else if (invalid) {
        opt.action = CliOptions::Action::Invalid;
    }
    return opt;
}

void HandleClipboardUpdate(HWND hwnd) {
    // A file list already exists (including our own write-back): leave it be.
    if (IsClipboardFormatAvailable(CF_HDROP)) return;
    if (!HasImageFormat()) return;

    MaybeCleanupOldPngs();

    CapturedImage image;
    std::wstring detail;
    const CaptureStatus captured =
        CaptureClipboardImage(hwnd, image, &detail);
    if (captured != CaptureStatus::Ok) {
        if (captured != CaptureStatus::NoImage &&
            captured != CaptureStatus::Stale) {
            LogError(L"capture failed (%d): %s", static_cast<int>(captured),
                     detail.c_str());
        }
        return;
    }

    if (!EnsureOutputDir()) {
        LogError(L"cannot create output directory");
        return;
    }

    const std::wstring path = MakeOutputPath();
    if (path.empty()) {
        LogError(L"cannot create output path");
        return;
    }

    if (!EncodeCapturedImage(image, path, &detail)) {
        LogError(L"encode failed: %s", detail.c_str());
        return;  // EncodeCapturedImage deletes the partial file.
    }

    const WriteStatus written =
        SetClipboardFile(hwnd, path, image.sequence, &detail);
    if (written != WriteStatus::Ok) {
        LogError(L"write-back failed (%d): %s", static_cast<int>(written),
                 detail.c_str());
        DeleteFileW(path.c_str());
        return;
    }

    Log(L"converted %ux%u %ubpp -> %s", image.info.width, image.info.height,
        image.info.bpp, path.c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
    switch (message) {
        case WM_CLIPBOARDUPDATE:
            try {
                HandleClipboardUpdate(hwnd);
            } catch (...) {
                LogError(L"unhandled exception in clipboard handler");
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            RemoveClipboardFormatListener(hwnd);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int RunDaemon(HINSTANCE instance, bool verbose) {
    SetVerbose(verbose);

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!mutex) {
        LogError(L"CreateMutex failed: %lu", GetLastError());
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    CoInitGuard com;
    if (!com.ok()) {
        LogHr(L"CoInitializeEx", com.hr());
        CloseHandle(mutex);
        return 1;
    }

    if (!EnsureOutputDir()) {
        LogError(L"cannot create output directory");
        CloseHandle(mutex);
        return 1;
    }
    CleanupOldPngs();
    MaybeCleanupOldPngs();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        LogError(L"RegisterClassExW failed: %lu", GetLastError());
        CloseHandle(mutex);
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd) {
        LogError(L"CreateWindowExW failed: %lu", GetLastError());
        CloseHandle(mutex);
        return 1;
    }
    if (!AddClipboardFormatListener(hwnd)) {
        LogError(L"AddClipboardFormatListener failed: %lu", GetLastError());
        DestroyWindow(hwnd);
        CloseHandle(mutex);
        return 1;
    }

    MSG message = {};
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CloseHandle(mutex);
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}

int Dispatch(HINSTANCE instance, const CliOptions& options) {
    SetVerbose(options.verbose);
    switch (options.action) {
        case CliOptions::Action::Daemon:
            return RunDaemon(instance, options.verbose);

        case CliOptions::Action::Install: {
            const std::wstring exe = GetExePath();
            if (exe.empty()) return 1;
            return InstallStartup(exe, options.verbose) ? 0 : 1;
        }

        case CliOptions::Action::Uninstall:
            return UninstallStartup(options.verbose) ? 0 : 1;

        case CliOptions::Action::Quit:
            return QuitRunningInstance(false, options.verbose) ? 0 : 1;

        case CliOptions::Action::SelfTest:
            return RunSelfTest(options.verbose);

        case CliOptions::Action::Help:
            ShowHelp(false);
            return 0;

        case CliOptions::Action::Invalid:
            ShowHelp(true);
            return 2;
    }
    return 2;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    const CliOptions options = ParseCli(commandLine);
    return Dispatch(instance, options);
}
