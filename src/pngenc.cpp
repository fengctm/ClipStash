#include "clipstash.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

DWORD ReadDword(const BYTE* p) {
    DWORD v = 0;
    memcpy(&v, p, sizeof(v));
    return v;
}

void ConvertBandToBgra(const BYTE* dib, const DibInfo& info, UINT y0,
                       UINT rows, BYTE* dst) {
    const BYTE* pixels = dib + info.dataOffset;
    for (UINT r = 0; r < rows; ++r) {
        const UINT y = y0 + r;
        const UINT srcRow = info.topDown ? y : info.height - 1 - y;
        const BYTE* src = pixels + static_cast<size_t>(srcRow) * info.srcStride;
        BYTE* out = dst + static_cast<size_t>(r) * info.width * 4;
        for (UINT x = 0; x < info.width; ++x) {
            if (info.bpp == 24) {
                out[x * 4 + 0] = src[x * 3 + 0];
                out[x * 4 + 1] = src[x * 3 + 1];
                out[x * 4 + 2] = src[x * 3 + 2];
                out[x * 4 + 3] = 255;
            } else {
                out[x * 4 + 0] = src[x * 4 + 0];
                out[x * 4 + 1] = src[x * 4 + 1];
                out[x * 4 + 2] = src[x * 4 + 2];
                out[x * 4 + 3] = info.preserveAlpha ? src[x * 4 + 3] : 255;
            }
        }
    }
}

std::vector<BYTE> MakeTestDib(UINT width, UINT height, UINT bpp) {
    const UINT stride = ((width * bpp + 31) / 32) * 4;
    std::vector<BYTE> dib(sizeof(BITMAPINFOHEADER) +
                              static_cast<size_t>(stride) * height,
                          0);
    BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(dib.data());
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = static_cast<LONG>(width);
    bih->biHeight = static_cast<LONG>(height);  // bottom-up
    bih->biPlanes = 1;
    bih->biBitCount = static_cast<WORD>(bpp);
    bih->biCompression = BI_RGB;
    bih->biSizeImage = stride * height;

    BYTE* bits = dib.data() + sizeof(BITMAPINFOHEADER);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            BYTE* p = bits + static_cast<size_t>(y) * stride +
                      static_cast<size_t>(x) * (bpp / 8);
            p[0] = static_cast<BYTE>(x * 3);
            p[1] = static_cast<BYTE>(y * 5);
            p[2] = 180;
            if (bpp == 32) p[3] = 0;  // deliberately invalid alpha
        }
    }
    return dib;
}

}  // namespace

bool ParseDib(const BYTE* data, SIZE_T size, DibInfo& info,
              std::wstring* detail) {
    auto fail = [&](const wchar_t* msg) {
        if (detail) *detail = msg;
        return false;
    };

    if (!data || size < sizeof(BITMAPINFOHEADER)) {
        return fail(L"DIB too small");
    }

    const BITMAPINFOHEADER* bih =
        reinterpret_cast<const BITMAPINFOHEADER*>(data);
    const UINT headerSize = bih->biSize;
    if (headerSize != 40 && headerSize != 52 && headerSize != 56 &&
        headerSize != 108 && headerSize != 124) {
        return fail(L"unsupported DIB header size");
    }
    if (headerSize > size) return fail(L"DIB header exceeds buffer");
    if (bih->biPlanes != 1) return fail(L"unsupported DIB planes");
    if (bih->biWidth <= 0 || bih->biWidth > 65535) {
        return fail(L"unsupported DIB width");
    }
    if (bih->biHeight == 0 || bih->biHeight == INT_MIN) {
        return fail(L"unsupported DIB height");
    }
    const LONG absHeight =
        bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;
    if (absHeight > 65535) return fail(L"unsupported DIB height");

    const UINT bpp = bih->biBitCount;
    if (bpp != 24 && bpp != 32) return fail(L"unsupported DIB bit count");
    if (bih->biCompression != BI_RGB && bih->biCompression != BI_BITFIELDS) {
        return fail(L"unsupported DIB compression");
    }

    bool hasAlphaMask = false;
    if (bih->biCompression == BI_BITFIELDS) {
        if (headerSize == 40) {
            return fail(L"40-byte BI_BITFIELDS is unsupported");
        }
        const DWORD red = ReadDword(data + 40);
        const DWORD green = ReadDword(data + 44);
        const DWORD blue = ReadDword(data + 48);
        const DWORD alpha = headerSize >= 56 ? ReadDword(data + 52) : 0;
        if (red != 0x00FF0000u || green != 0x0000FF00u ||
            blue != 0x000000FFu) {
            return fail(L"nonstandard DIB color masks");
        }
        if (alpha != 0 && alpha != 0xFF000000u) {
            return fail(L"nonstandard DIB alpha mask");
        }
        hasAlphaMask = (alpha == 0xFF000000u);
    } else if (headerSize >= 56) {
        const DWORD alpha = ReadDword(data + 52);
        if (alpha == 0xFF000000u) {
            hasAlphaMask = true;
        } else if (alpha != 0) {
            return fail(L"nonstandard DIB alpha mask");
        }
    }
    if (bpp == 24) hasAlphaMask = false;

    const uint64_t stride =
        ((static_cast<uint64_t>(bih->biWidth) * bpp + 31) / 32) * 4;
    const uint64_t needed =
        static_cast<uint64_t>(headerSize) + stride * absHeight;
    if (needed > static_cast<uint64_t>(size)) {
        return fail(L"DIB pixel data truncated");
    }
    if (needed > kMaxDibBytes) return fail(L"DIB too large");
    if (stride > UINT_MAX) return fail(L"DIB stride too large");

    info.width = static_cast<UINT>(bih->biWidth);
    info.height = static_cast<UINT>(absHeight);
    info.bpp = bpp;
    info.srcStride = static_cast<UINT>(stride);
    info.dataOffset = headerSize;
    info.topDown = bih->biHeight < 0;
    info.hasAlphaMask = hasAlphaMask;
    info.preserveAlpha = hasAlphaMask;
    if (detail) detail->clear();
    return true;
}

void ApplyAlphaPolicy(const BYTE* data, DibInfo& info) {
    if (!data || info.bpp != 32 || !info.hasAlphaMask) {
        info.preserveAlpha = false;
        return;
    }
    const BYTE* pixels = data + info.dataOffset;
    const size_t count = static_cast<size_t>(info.width) * info.height;
    for (size_t i = 0; i < count; ++i) {
        if (pixels[i * 4 + 3] != 0) {
            info.preserveAlpha = true;
            return;
        }
    }
    info.preserveAlpha = false;
}

bool EncodeCapturedImage(const CapturedImage& img, const std::wstring& path,
                         std::wstring* detail) {
    if (detail) detail->clear();
    bool ok = false;
    HRESULT hr = E_FAIL;

    try {
        do {
            if (img.info.width == 0 || img.info.height == 0 ||
                img.info.bpp == 0) {
                if (detail) *detail = L"invalid image info";
                break;
            }

            ComPtr<IWICImagingFactory> factory;
            hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
            if (FAILED(hr)) break;

            ComPtr<IWICStream> stream;
            hr = factory->CreateStream(&stream);
            if (FAILED(hr)) break;
            hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
            if (FAILED(hr)) break;

            ComPtr<IWICBitmapEncoder> encoder;
            hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                        &encoder);
            if (FAILED(hr)) break;
            hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (FAILED(hr)) break;

            ComPtr<IWICBitmapFrameEncode> frame;
            ComPtr<IPropertyBag2> props;
            hr = encoder->CreateNewFrame(&frame, &props);
            if (FAILED(hr)) break;
            hr = frame->Initialize(props.Get());
            if (FAILED(hr)) break;

            hr = frame->SetSize(img.info.width, img.info.height);
            if (FAILED(hr)) break;

            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            hr = frame->SetPixelFormat(&format);
            if (FAILED(hr)) break;
            if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
                hr = E_FAIL;
                if (detail) *detail = L"WIC negotiated an unexpected format";
                break;
            }

            UINT bandRows = kBandRows;
            const UINT maxRows =
                (1u << 20) / (img.info.width * 4);
            if (maxRows == 0) {
                bandRows = 1;
            } else if (bandRows > maxRows) {
                bandRows = maxRows;
            }

            std::vector<BYTE> band(
                static_cast<size_t>(bandRows) * img.info.width * 4);
            for (UINT y = 0; y < img.info.height; y += bandRows) {
                const UINT rows =
                    std::min(bandRows, img.info.height - y);
                ConvertBandToBgra(img.dib.data(), img.info, y, rows,
                                  band.data());
                hr = frame->WritePixels(
                    rows, img.info.width * 4, rows * img.info.width * 4,
                    band.data());
                if (FAILED(hr)) break;
            }
            if (FAILED(hr)) break;

            hr = frame->Commit();
            if (FAILED(hr)) break;
            hr = encoder->Commit();
            if (FAILED(hr)) break;

            ok = true;
        } while (false);
    } catch (...) {
        ok = false;
        hr = E_OUTOFMEMORY;
    }

    if (!ok) {
        DeleteFileW(path.c_str());
        if (detail && detail->empty()) {
            wchar_t buf[96] = {};
            swprintf_s(buf, L"WIC failure 0x%08X", static_cast<unsigned>(hr));
            *detail = buf;
        }
    }
    return ok;
}

bool VerifyPng(const std::wstring& path, UINT expectedW, UINT expectedH) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    BYTE hdr[33] = {};
    DWORD read = 0;
    const BOOL readOk = ReadFile(f, hdr, sizeof(hdr), &read, nullptr);
    CloseHandle(f);
    if (!readOk || read < sizeof(hdr)) return false;

    static const BYTE signature[8] = {0x89, 'P', 'N', 'G',
                                      0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(hdr, signature, sizeof(signature)) != 0) return false;
    if (memcmp(hdr + 12, "IHDR", 4) != 0) return false;

    const UINT width = (static_cast<UINT>(hdr[16]) << 24) |
                       (static_cast<UINT>(hdr[17]) << 16) |
                       (static_cast<UINT>(hdr[18]) << 8) | hdr[19];
    const UINT height = (static_cast<UINT>(hdr[20]) << 24) |
                        (static_cast<UINT>(hdr[21]) << 16) |
                        (static_cast<UINT>(hdr[22]) << 8) | hdr[23];
    return width == expectedW && height == expectedH;
}

int RunSelfTest(bool verbose) {
    (void)verbose;
    try {
        CoInitGuard co;
        if (!co.ok()) {
            LogHr(L"CoInitializeEx", co.hr());
            return 1;
        }
        if (!EnsureOutputDir()) {
            LogError(L"selftest: cannot create output directory");
            return 1;
        }
        const std::wstring dir = GetOutputDir();
        if (dir.empty()) return 1;

        int failures = 0;
        const UINT bpps[2] = {24, 32};
        for (UINT bpp : bpps) {
            auto dib = MakeTestDib(200, 120, bpp);
            DibInfo info;
            std::wstring detail;
            if (!ParseDib(dib.data(), dib.size(), info, &detail)) {
                LogError(L"selftest %u bpp: ParseDib failed: %s", bpp,
                    detail.c_str());
                ++failures;
                continue;
            }
            ApplyAlphaPolicy(dib.data(), info);

            CapturedImage img;
            img.dib = std::move(dib);
            img.info = info;
            img.sourceFormat = CF_DIB;

            std::wstring path =
                dir + L"\\selftest_" + std::to_wstring(bpp) + L".png";
            if (!EncodeCapturedImage(img, path, &detail)) {
                LogError(L"selftest %u bpp: encode failed: %s", bpp,
                    detail.c_str());
                ++failures;
                continue;
            }
            if (!VerifyPng(path, 200, 120)) {
                LogError(L"selftest %u bpp: PNG verification failed", bpp);
                ++failures;
            }
        }
        return failures == 0 ? 0 : 1;
    } catch (...) {
        LogError(L"selftest: unexpected exception");
        return 1;
    }
}
