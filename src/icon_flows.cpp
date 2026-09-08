// M3c T3: 图标导入/管理 IPC 流程（saveIcon/icons/readIcon）。
// 文件均位于 %APPDATA%\YiPie\icons\；name 已由 router 层校验字符集。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

#include <cwctype>
#include <string>
#include <vector>

#include "rapidjson/document.h"

#include "icon_flows.h"
#include "icon_extract.h"

using rapidjson::Document;

namespace iconflows {

std::wstring IconsDir() {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base))) return {};
    std::wstring dir = std::wstring(base) + L"\\YiPie\\icons";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

namespace {
std::wstring U8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string W2U(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                                      nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
}  // namespace

HostResult SaveIcon(const std::string& argsText) {
    HostResult r;
    Document d;
    d.Parse(argsText.c_str());
    if (d.HasParseError() || !d.IsObject() || !d.HasMember("name") ||
        !d["name"].IsString() || !d.HasMember("base64Png") ||
        !d["base64Png"].IsString()) {
        r.error = "bad icon args";
        return r;
    }
    const std::string name = d["name"].GetString();
    const std::string b64 = d["base64Png"].GetString();

    DWORD rawLen = 0;
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
                              nullptr, &rawLen, nullptr, nullptr) ||
        rawLen == 0 || rawLen > 2u * 1024u * 1024u) {
        r.error = "base64 invalid or too large";
        return r;
    }
    std::vector<BYTE> raw(rawLen);
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
                              raw.data(), &rawLen, nullptr, nullptr)) {
        r.error = "base64 decode failed";
        return r;
    }
    if (rawLen < 8 || raw[0] != 0x89 || raw[1] != 'P' || raw[2] != 'N' || raw[3] != 'G') {
        r.error = "not a PNG";
        return r;
    }
    const std::wstring dir = IconsDir();
    if (dir.empty()) { r.error = "icons dir unavailable"; return r; }
    const std::wstring path = dir + L"\\" + U8(name);
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) { r.error = "write failed"; return r; }
    DWORD written = 0;
    const BOOL okw = WriteFile(hf, raw.data(), rawLen, &written, nullptr);
    CloseHandle(hf);
    if (!okw || written != rawLen) { r.error = "short write"; return r; }
    r.ok = true;
    r.resultJson = "{\"iconKey\":\"f:" + name + "\"}";
    return r;
}

HostResult ListIcons() {
    HostResult r;
    const std::wstring dir = IconsDir();
    if (dir.empty()) { r.error = "icons dir unavailable"; return r; }
    std::string files = "[";
    WIN32_FIND_DATAW fd{};
    HANDLE hq = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
    if (hq != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (files.size() > 1) files += ",";
            files += "\"" + W2U(fd.cFileName) + "\"";
        } while (FindNextFileW(hq, &fd));
        FindClose(hq);
    }
    files += "]";
    r.ok = true;
    r.resultJson = "{\"files\":" + files + "}";
    return r;
}

HostResult DeleteIcon(const std::string& name, const IsReferencedFn& referenced) {
    HostResult r;
    if (referenced && referenced("f:" + name)) {
        r.ok = true;
        r.resultJson = "{\"inUse\":true}";
        return r;
    }
    const std::wstring dir = IconsDir();
    if (dir.empty()) { r.error = "icons dir unavailable"; return r; }
    const std::wstring path = dir + L"\\" + U8(name);
    if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        r.error = "delete failed";
        return r;
    }
    r.ok = true;
    r.resultJson = "{\"deleted\":true}";
    return r;
}

HostResult ReadIcon(const std::string& name) {
    HostResult r;
    const std::wstring dir = IconsDir();
    if (dir.empty()) { r.error = "icons dir unavailable"; return r; }
    const std::wstring path = dir + L"\\" + U8(name);
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) { r.error = "icon not found"; return r; }
    const DWORD size = GetFileSize(hf, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 2u * 1024u * 1024u) {
        CloseHandle(hf);
        r.error = "bad icon size";
        return r;
    }
    std::vector<BYTE> raw(size);
    DWORD got = 0;
    const BOOL okr = ReadFile(hf, raw.data(), size, &got, nullptr);
    CloseHandle(hf);
    if (!okr || got != size) { r.error = "read failed"; return r; }
    DWORD b64Len = 0;
    CryptBinaryToStringA(raw.data(), size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         nullptr, &b64Len);
    std::string b64(b64Len, '\0');
    CryptBinaryToStringA(raw.data(), size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         b64.data(), &b64Len);
    while (!b64.empty() && (b64.back() == '\0' || b64.back() == '\r' || b64.back() == '\n'))
        b64.pop_back();
    r.ok = true;
    r.resultJson = "{\"base64\":\"" + b64 + "\"}";
    return r;
}


namespace {
// HGLOBAL IStream -> 字节 vector
std::vector<BYTE> HGlobalToVec(IStream* st) {
    std::vector<BYTE> out;
    HGLOBAL hg = nullptr;
    if (SUCCEEDED(GetHGlobalFromStream(st, &hg))) {
        const SIZE_T sz = GlobalSize(hg);
        void* p = GlobalLock(hg);
        if (p && sz) { out.assign((BYTE*)p, (BYTE*)p + sz); }
        GlobalUnlock(hg);
    }
    return out;
}
}  // namespace

HostResult PreviewIcon(const std::string& target) {
    HostResult r;
    const std::wstring path = U8(target);
    if (path.empty() || path.size() > 512) { r.error = "bad target"; return r; }

    // 共享提取器：解析快捷方式目标 + 高分辨率 + 无箭头角标（与渲染一致，问题2）
    HICON hIcon = ExtractCleanIcon(path, 64);
    if (!hIcon) { r.error = "no icon"; return r; }
    HostResult result = r;
    result.error = "encode failed";

    // 画进 64x64 top-down BGRA DIB
    HDC screen = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 64;
    bi.bmiHeader.biHeight = -64;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    IWICImagingFactory* wic = nullptr;
    if (dib && bits && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        memset(bits, 0, 64 * 64 * 4);
        HGDIOBJ old = SelectObject(memDC, dib);
        DrawIconEx(memDC, 0, 0, hIcon, 64, 64, 0, nullptr, DI_NORMAL);
        SelectObject(memDC, old);
        // 无 alpha 通道的经典图标：非黑像素视为不透明
        BYTE* px = (BYTE*)bits;
        bool anyAlpha = false;
        for (int i = 3; i < 64 * 64 * 4; i += 4) if (px[i]) { anyAlpha = true; break; }
        if (!anyAlpha)
            for (int i = 0; i < 64 * 64 * 4; i += 4)
                px[i + 3] = (px[i] | px[i + 1] | px[i + 2]) ? 255 : 0;

        IWICBitmap* wbmp = nullptr;
        IWICStream* stream = nullptr;
        IStream* istream = nullptr;
        IWICBitmapEncoder* enc = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        IPropertyBag2* bag = nullptr;
        GUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(wic->CreateBitmapFromMemory(64, 64, fmt, 64 * 4, 64 * 64 * 4,
                                                  px, &wbmp)) &&
            SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &istream)) &&
            SUCCEEDED(wic->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromIStream(istream)) &&
            SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
            SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(enc->CreateNewFrame(&frame, &bag))) {
            GUID vendor = GUID_ContainerFormatPng;
            frame->Initialize(nullptr);
            frame->SetSize(64, 64);
            frame->SetPixelFormat(&vendor);
            if (SUCCEEDED(frame->WriteSource(wbmp, nullptr)) &&
                SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) {
                std::vector<BYTE> png = HGlobalToVec(istream);
                if (!png.empty()) {
                    DWORD b64Len = 0;
                    CryptBinaryToStringA(png.data(), (DWORD)png.size(),
                        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64Len);
                    std::string b64(b64Len, '\0');
                    CryptBinaryToStringA(png.data(), (DWORD)png.size(),
                        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &b64Len);
                    while (!b64.empty() && (b64.back() == '\0' || b64.back() == '\n' || b64.back() == '\r'))
                        b64.pop_back();
                    result.ok = true;
                    result.resultJson = "{\"base64\":\"" + b64 + "\"}";
                }
            }
        }
        if (bag) bag->Release();
        if (frame) frame->Release();
        if (enc) enc->Release();
        if (stream) stream->Release();
        if (istream) istream->Release();
        if (wbmp) wbmp->Release();
    }
    if (wic) wic->Release();
    if (dib) DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screen);
    DestroyIcon(hIcon);
    return result;
}

}  // namespace iconflows
