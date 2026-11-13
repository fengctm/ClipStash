#include "clipstash.h"

#include <shlobj.h>
#include <shellapi.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

class ClipboardLock {
public:
    ClipboardLock() = default;
    ~ClipboardLock() { Close(); }
    ClipboardLock(const ClipboardLock&) = delete;
    ClipboardLock& operator=(const ClipboardLock&) = delete;

    bool Open(HWND hwnd, int attempts, DWORD delayMs) {
        for (int i = 0; i < attempts; ++i) {
            if (OpenClipboard(hwnd)) {
                open_ = true;
                return true;
            }
            if (i + 1 < attempts) Sleep(delayMs);
        }
        return false;
    }

    void Close() {
        if (open_) {
            CloseClipboard();
            open_ = false;
        }
    }

private:
    bool open_ = false;
};

class GlobalMem {
public:
    explicit GlobalMem(HGLOBAL handle = nullptr) : handle_(handle) {}
    ~GlobalMem() {
        if (handle_) GlobalFree(handle_);
    }
    GlobalMem(const GlobalMem&) = delete;
    GlobalMem& operator=(const GlobalMem&) = delete;

    HGLOBAL get() const { return handle_; }
    HGLOBAL release() {
        HGLOBAL out = handle_;
        handle_ = nullptr;
        return out;
    }

private:
    HGLOBAL handle_ = nullptr;
};

bool CopyDibHandle(HANDLE handle, UINT format, CapturedImage& out,
                   std::wstring* detail) {
    if (!handle) return false;
    HGLOBAL global = static_cast<HGLOBAL>(handle);
    const SIZE_T size = GlobalSize(global);
    if (size == 0) {
        if (detail) *detail = L"empty clipboard DIB";
        return false;
    }

    const BYTE* locked = static_cast<const BYTE*>(GlobalLock(global));
    if (!locked) {
        if (detail) *detail = L"GlobalLock failed";
        return false;
    }

    DibInfo info;
    bool ok = ParseDib(locked, size, info, detail);
    if (ok) {
        const uint64_t needed64 =
            static_cast<uint64_t>(info.dataOffset) +
            static_cast<uint64_t>(info.srcStride) * info.height;
        if (needed64 > static_cast<uint64_t>(size)) {
            ok = false;
            if (detail) *detail = L"DIB data exceeds clipboard buffer";
        } else {
            const SIZE_T needed = static_cast<SIZE_T>(needed64);
            out.dib.assign(locked, locked + needed);
            out.info = info;
            out.sourceFormat = format;
            ApplyAlphaPolicy(out.dib.data(), out.info);
        }
    }
    GlobalUnlock(global);
    return ok;
}

bool CopyBitmapHandle(HBITMAP bitmap, CapturedImage& out,
                      std::wstring* detail) {
    if (!bitmap) return false;

    BITMAP bm = {};
    if (!GetObjectW(bitmap, sizeof(bm), &bm)) {
        if (detail) *detail = L"GetObject failed";
        return false;
    }
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0 || bm.bmWidth > 65535 ||
        bm.bmHeight > 65535) {
        if (detail) *detail = L"unsupported HBITMAP size";
        return false;
    }

    const UINT width = static_cast<UINT>(bm.bmWidth);
    const UINT height = static_cast<UINT>(bm.bmHeight);
    const uint64_t size64 = sizeof(BITMAPINFOHEADER) +
                            static_cast<uint64_t>(width) * height * 4;
    if (size64 > kMaxDibBytes) {
        if (detail) *detail = L"HBITMAP too large";
        return false;
    }

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = static_cast<LONG>(width);
    bih.biHeight = static_cast<LONG>(height);
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = width * height * 4;

    std::vector<BYTE> dib(static_cast<size_t>(size64), 0);
    memcpy(dib.data(), &bih, sizeof(bih));

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        if (detail) *detail = L"CreateCompatibleDC failed";
        return false;
    }
    const int lines = GetDIBits(dc, bitmap, 0, height,
                                dib.data() + sizeof(bih),
                                reinterpret_cast<BITMAPINFO*>(&bih),
                                DIB_RGB_COLORS);
    DeleteDC(dc);
    if (lines != static_cast<int>(height)) {
        if (detail) *detail = L"GetDIBits failed";
        return false;
    }

    DibInfo info;
    if (!ParseDib(dib.data(), dib.size(), info, detail)) return false;
    out.dib = std::move(dib);
    out.info = info;
    out.sourceFormat = CF_BITMAP;
    ApplyAlphaPolicy(out.dib.data(), out.info);
    return true;
}

HGLOBAL BuildDropFiles(const std::wstring& path) {
    if (path.empty()) return nullptr;

    const uint64_t bytes64 =
        sizeof(DROPFILES) +
        (static_cast<uint64_t>(path.size()) + 2) * sizeof(wchar_t);
    if (bytes64 > MAXDWORD) return nullptr;

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                 static_cast<SIZE_T>(bytes64));
    if (!handle) return nullptr;

    DROPFILES* drop = static_cast<DROPFILES*>(GlobalLock(handle));
    if (!drop) {
        GlobalFree(handle);
        return nullptr;
    }

    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    wchar_t* dst = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
    memcpy(dst, path.c_str(), path.size() * sizeof(wchar_t));
    // GMEM_ZEROINIT already provides the two trailing NULs.
    GlobalUnlock(handle);
    return handle;
}

}  // namespace

bool HasImageFormat() {
    return IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(CF_DIB) ||
           IsClipboardFormatAvailable(CF_BITMAP);
}

CaptureStatus CaptureClipboardImage(HWND hwnd, CapturedImage& out,
                                    std::wstring* detail) {
    out = CapturedImage{};
    const DWORD sequence = GetClipboardSequenceNumber();

    ClipboardLock lock;
    if (!lock.Open(hwnd, kOpenRetries, kOpenRetryDelayMs)) {
        if (detail) *detail = L"OpenClipboard failed";
        return CaptureStatus::OpenFailed;
    }
    if (GetClipboardSequenceNumber() != sequence) {
        if (detail) *detail = L"clipboard changed while reading";
        return CaptureStatus::Stale;
    }

    bool sawImageFormat = false;

    if (IsClipboardFormatAvailable(CF_DIBV5)) {
        sawImageFormat = true;
        if (CopyDibHandle(GetClipboardData(CF_DIBV5), CF_DIBV5, out, detail)) {
            out.sequence = sequence;
            return CaptureStatus::Ok;
        }
    }
    if (IsClipboardFormatAvailable(CF_DIB)) {
        sawImageFormat = true;
        if (CopyDibHandle(GetClipboardData(CF_DIB), CF_DIB, out, detail)) {
            out.sequence = sequence;
            return CaptureStatus::Ok;
        }
    }
    if (IsClipboardFormatAvailable(CF_BITMAP)) {
        sawImageFormat = true;
        HBITMAP bitmap =
            static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if (CopyBitmapHandle(bitmap, out, detail)) {
            out.sequence = sequence;
            return CaptureStatus::Ok;
        }
    }

    return sawImageFormat ? CaptureStatus::Unsupported
                          : CaptureStatus::NoImage;
}

WriteStatus SetClipboardFile(HWND hwnd, const std::wstring& path,
                             DWORD expectedSeq, std::wstring* detail) {
    GlobalMem memory(BuildDropFiles(path));
    if (!memory.get()) {
        if (detail) *detail = L"GlobalAlloc for CF_HDROP failed";
        return WriteStatus::AllocFailed;
    }

    ClipboardLock lock;
    if (!lock.Open(hwnd, kOpenRetries, kOpenRetryDelayMs)) {
        if (detail) *detail = L"OpenClipboard failed";
        return WriteStatus::OpenFailed;
    }
    if (GetClipboardSequenceNumber() != expectedSeq) {
        if (detail) *detail = L"clipboard changed before write-back";
        return WriteStatus::Stale;
    }
    if (!EmptyClipboard()) {
        if (detail) *detail = L"EmptyClipboard failed";
        return WriteStatus::Failed;
    }

    HGLOBAL handle = memory.release();
    if (!SetClipboardData(CF_HDROP, handle)) {
        GlobalFree(handle);
        if (detail) *detail = L"SetClipboardData failed";
        return WriteStatus::Failed;
    }

    // Ownership transferred to the system on success. CloseClipboard is
    // handled by the RAII lock.
    return WriteStatus::Ok;
}
