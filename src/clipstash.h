#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <string>
#include <vector>

inline constexpr wchar_t kWindowClass[] = L"ClipStash.MessageWindow.v1";
inline constexpr wchar_t kMutexName[] = L"Local\\ClipStash.Singleton";
inline constexpr wchar_t kRunKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"ClipStash";

inline constexpr UINT kBandRows = 32;
inline constexpr int kOpenRetries = 3;
inline constexpr DWORD kOpenRetryDelayMs = 15;
constexpr SIZE_T kMaxDibBytes = 256ull * 1024 * 1024;
constexpr ULONGLONG kMaxAge100ns = 7ull * 24 * 60 * 60 * 10000000ull;

struct DibInfo {
    UINT width = 0;
    UINT height = 0;      // always positive
    UINT bpp = 0;         // 24 or 32
    UINT srcStride = 0;
    UINT dataOffset = 0;
    bool topDown = false;
    bool hasAlphaMask = false;
    bool preserveAlpha = false;
};

struct CapturedImage {
    std::vector<BYTE> dib;
    DibInfo info;
    UINT sourceFormat = 0;
    DWORD sequence = 0;
};

enum class CaptureStatus {
    Ok,
    NoImage,
    Stale,
    OpenFailed,
    Unsupported,
    TooLarge,
    Failed
};

enum class WriteStatus {
    Ok,
    Stale,
    OpenFailed,
    AllocFailed,
    Failed
};

struct CliOptions {
    enum class Action {
        Daemon,
        Install,
        Uninstall,
        Quit,
        SelfTest,
        Help,
        Invalid
    };

    Action action = Action::Daemon;
    bool verbose = false;
};

class CoInitGuard {
public:
    CoInitGuard() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~CoInitGuard() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    CoInitGuard(const CoInitGuard&) = delete;
    CoInitGuard& operator=(const CoInitGuard&) = delete;

    bool ok() const { return SUCCEEDED(hr_); }
    HRESULT hr() const { return hr_; }

private:
    HRESULT hr_;
};

// clipboard.cpp
bool HasImageFormat();
CaptureStatus CaptureClipboardImage(HWND hwnd, CapturedImage& out,
                                    std::wstring* detail);
WriteStatus SetClipboardFile(HWND hwnd, const std::wstring& path,
                             DWORD expectedSeq, std::wstring* detail);

// pngenc.cpp
bool ParseDib(const BYTE* data, SIZE_T size, DibInfo& info,
              std::wstring* detail);
void ApplyAlphaPolicy(const BYTE* data, DibInfo& info);
bool EncodeCapturedImage(const CapturedImage& img, const std::wstring& path,
                         std::wstring* detail);
bool VerifyPng(const std::wstring& path, UINT expectedW, UINT expectedH);
int RunSelfTest(bool verbose);

// lifecycle.cpp
std::wstring GetExePath();
std::wstring GetOutputDir();
bool EnsureOutputDir();
std::wstring MakeOutputPath();
int CleanupOldPngs();
void MaybeCleanupOldPngs();
bool InstallStartup(const std::wstring& exePath, bool verbose);
bool UninstallStartup(bool verbose);
bool StartDaemon(const std::wstring& exePath);
HWND FindDaemonWindow();
bool QuitRunningInstance(bool wait, bool verbose);
void SetVerbose(bool verbose);
bool IsVerbose();
void Log(const wchar_t* fmt, ...);
void LogError(const wchar_t* fmt, ...);
void LogHr(const wchar_t* what, HRESULT hr);
