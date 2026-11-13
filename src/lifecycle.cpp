#include "clipstash.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {

bool g_verbose = false;

std::wstring Join(const std::wstring& dir, const wchar_t* name) {
    if (dir.empty()) return {};
    return dir + L"\\" + name;
}

std::wstring GetLocalAppDataDir() {
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (len == 0) return {};
    std::wstring buf(len, L'\0');
    DWORD got = GetEnvironmentVariableW(L"LOCALAPPDATA", buf.data(), len);
    if (got == 0 || got >= len) return {};
    buf.resize(got);
    if (!buf.empty() && buf.back() != L'\\') buf.push_back(L'\\');
    return buf + L"ClipStash";
}

std::wstring GetLogDir() {
    std::wstring dir = GetLocalAppDataDir();
    if (!dir.empty()) return dir;
    return GetOutputDir();
}

void TrimLogIfNeeded() {
    std::wstring dir = GetLogDir();
    if (dir.empty()) return;
    std::wstring path = Join(dir, L"clipstash.log");
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        return;
    }
    ULARGE_INTEGER size = {};
    size.LowPart = fad.nFileSizeLow;
    size.HighPart = fad.nFileSizeHigh;
    if (size.QuadPart <= 1024ull * 1024ull) return;

    std::wstring old = path + L".old";
    if (!MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(path.c_str());
    }
}

void WriteLogV(const wchar_t* fmt, va_list args, bool force) {
    if (!force && !g_verbose) return;
    try {
        std::wstring dir = GetLogDir();
        if (dir.empty()) return;
        CreateDirectoryW(dir.c_str(), nullptr);

        wchar_t buf[2048] = {};
        _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);

        std::wstring path = Join(dir, L"clipstash.log");
        HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(f, buf, static_cast<DWORD>(wcslen(buf) * sizeof(wchar_t)),
                  &written, nullptr);
        WriteFile(f, L"\r\n", 4, &written, nullptr);
        CloseHandle(f);

        if (force) TrimLogIfNeeded();
    } catch (...) {
        // Logging must never affect clipboard behavior.
    }
}

}  // namespace

void SetVerbose(bool verbose) {
    g_verbose = verbose;
    try {
        TrimLogIfNeeded();
    } catch (...) {
        // Logging must never affect startup.
    }
}

bool IsVerbose() {
    return g_verbose;
}

void Log(const wchar_t* fmt, ...) {
    if (!g_verbose) return;
    va_list args;
    va_start(args, fmt);
    WriteLogV(fmt, args, false);
    va_end(args);
}

void LogError(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    WriteLogV(fmt, args, true);
    va_end(args);
}

void LogHr(const wchar_t* what, HRESULT hr) {
    LogError(L"%s failed: 0x%08X", what, static_cast<unsigned>(hr));
}

std::wstring GetExePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(),
                                     static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
}

std::wstring GetOutputDir() {
    DWORD len = GetTempPathW(0, nullptr);
    if (len == 0) return {};
    std::wstring tmp(len, L'\0');
    DWORD n = GetTempPathW(len, tmp.data());
    if (n == 0 || n >= len) return {};
    tmp.resize(n);
    if (tmp.empty() || tmp.back() != L'\\') tmp.push_back(L'\\');
    return tmp + L"ClipStash";
}

bool EnsureOutputDir() {
    std::wstring dir = GetOutputDir();
    if (dir.empty()) return false;
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attrs = GetFileAttributesW(dir.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring MakeOutputPath() {
    std::wstring dir = GetOutputDir();
    if (dir.empty()) return {};

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    static unsigned counter = 0;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        wchar_t name[128] = {};
        swprintf_s(name, L"%04d%02d%02d_%02d%02d%02d_%03u_%03u.png",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                   st.wSecond, static_cast<unsigned>(st.wMilliseconds),
                   counter++);
        std::wstring path = Join(dir, name);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }
    return {};
}

int CleanupOldPngs() {
    std::wstring dir = GetOutputDir();
    if (dir.empty()) return 0;

    std::wstring pattern = Join(dir, L"*.png");
    WIN32_FIND_DATAW fd = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return 0;

    FILETIME nowFt = {};
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now = {};
    now.LowPart = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;
    const ULONGLONG cutoff =
        now.QuadPart > kMaxAge100ns ? now.QuadPart - kMaxAge100ns : 0;

    int deleted = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        ULARGE_INTEGER written = {};
        written.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        written.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        if (written.QuadPart >= cutoff) continue;
        std::wstring path = Join(dir, fd.cFileName);
        if (DeleteFileW(path.c_str())) ++deleted;
    } while (FindNextFileW(find, &fd));
    FindClose(find);

    if (deleted > 0) Log(L"cleanup deleted %d PNG(s)", deleted);
    return deleted;
}

void MaybeCleanupOldPngs() {
    static ULONGLONG last = 0;
    static bool initialized = false;
    const ULONGLONG now = GetTickCount64();
    if (!initialized) {
        last = now;
        initialized = true;
        return;
    }
    constexpr ULONGLONG kDayMs = 24ull * 60 * 60 * 1000;
    if (now - last < kDayMs) return;
    last = now;
    CleanupOldPngs();
}

bool StartDaemon(const std::wstring& exePath) {
    if (exePath.empty()) return false;
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        LogError(L"CreateProcessW failed: %lu", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool InstallStartup(const std::wstring& exePath, bool verbose) {
    (void)verbose;
    if (exePath.empty()) return false;

    const std::wstring quoted = L"\"" + exePath + L"\"";
    HKEY key = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                 KEY_SET_VALUE, nullptr, &key, nullptr);
    if (st != ERROR_SUCCESS) {
        LogError(L"RegCreateKeyExW failed: %ld", st);
        return false;
    }
    st = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(quoted.c_str()),
                        static_cast<DWORD>((quoted.size() + 1) *
                                           sizeof(wchar_t)));
    RegCloseKey(key);
    if (st != ERROR_SUCCESS) {
        LogError(L"RegSetValueExW failed: %ld", st);
        return false;
    }

    Log(L"installed: %s", quoted.c_str());
    if (!FindDaemonWindow()) StartDaemon(exePath);
    return true;
}

bool UninstallStartup(bool verbose) {
    (void)verbose;
    QuitRunningInstance(true, verbose);

    HKEY key = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE,
                               &key);
    if (st == ERROR_FILE_NOT_FOUND) return true;
    if (st != ERROR_SUCCESS) {
        LogError(L"RegOpenKeyExW failed: %ld", st);
        return false;
    }
    st = RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
    if (st == ERROR_FILE_NOT_FOUND) return true;
    if (st != ERROR_SUCCESS) {
        LogError(L"RegDeleteValueW failed: %ld", st);
        return false;
    }
    Log(L"uninstalled");
    return true;
}

HWND FindDaemonWindow() {
    return FindWindowExW(HWND_MESSAGE, nullptr, kWindowClass, nullptr);
}

bool QuitRunningInstance(bool wait, bool verbose) {
    (void)verbose;
    HWND hwnd = FindDaemonWindow();
    if (!hwnd) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!PostMessageW(hwnd, WM_CLOSE, 0, 0)) {
        LogError(L"PostMessage(WM_CLOSE) failed: %lu", GetLastError());
        return true;
    }
    if (wait && pid != 0) {
        HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (proc) {
            DWORD wr = WaitForSingleObject(proc, 3000);
            if (wr == WAIT_TIMEOUT) LogError(L"daemon did not exit within 3s");
            CloseHandle(proc);
        }
    }
    return true;
}
