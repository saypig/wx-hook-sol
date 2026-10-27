// paths.h -- deployment directory and log path
#pragma once
#include <windows.h>
#include <cstdio>
#include <string>

namespace wxhook {
namespace paths {

inline std::string w2u(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

inline bool readAllW(const std::wstring& p, std::string* out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[1024] = {0};
    DWORD n = 0;
    ReadFile(h, buf, sizeof buf - 1, &n, nullptr);
    CloseHandle(h);
    out->assign(buf, n);
    return n > 0;
}

inline std::wstring selfPathW() {
    HMODULE hm = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(const void*)&selfPathW, &hm);
    wchar_t p[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(hm, p, MAX_PATH);
    return std::wstring(p, n ? n : 0);
}

inline std::wstring dirOfW(const std::wstring& file) {
    size_t k = file.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? std::wstring() : file.substr(0, k);
}

inline std::wstring sidecarOfW(const std::wstring& self) {
    std::wstring s = self;
    size_t d = s.find_last_of(L'.');
    size_t k = s.find_last_of(L"\\/");
    if (d != std::wstring::npos && (k == std::wstring::npos || d > k)) s.resize(d);
    return s + L".home";
}

inline const std::wstring& homeW() {
    static std::wstring h = [] {
        const std::wstring self = selfPathW();
        const std::wstring dir = dirOfW(self);
        std::string s;
        if (readAllW(sidecarOfW(self), &s)) {
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                s.pop_back();
            if (!s.empty()) {
                int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
                std::wstring w((size_t)n, 0);
                MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
                return w;
            }
        }
        return dir;
    }();
    return h;
}

inline std::string homeA() {
    static std::string a = w2u(homeW());
    return a;
}

inline std::string p(const char* sub) {
    std::string s = homeA();
    if (sub && *sub) {
        s += '\\';
        s += sub;
    }
    return s;
}

inline std::string logFile() { return p("wxhook_out.txt"); }

inline std::string logFileForPort(int port) {
    if (port <= 0 || port == 30002) return logFile();
    char n[64];
    _snprintf(n, sizeof n, "wxhook_out_%d.txt", port);
    n[sizeof n - 1] = 0;
    return p(n);
}

} // namespace paths
} // namespace wxhook
