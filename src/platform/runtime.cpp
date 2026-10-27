// runtime.cpp -- logging, module base, safe reads, WeixinString
#include "platform/runtime.h"
#include "platform/paths.h"
#include "platform/offsets.h"
#include <psapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace wxhook {
namespace platform {

static std::string g_logPath = paths::logFile();
static CRITICAL_SECTION g_logCs;
static bool g_logInit = false;

static void ensureLogCs() {
    if (!g_logInit) {
        InitializeCriticalSection(&g_logCs);
        g_logInit = true;
    }
}

void setLogPort(int port) {
    ensureLogCs();
    EnterCriticalSection(&g_logCs);
    g_logPath = paths::logFileForPort(port);
    LeaveCriticalSection(&g_logCs);
}

void logf(const char* fmt, ...) {
    ensureLogCs();
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    buf[n] = '\n';
    buf[n + 1] = 0;
    EnterCriticalSection(&g_logCs);
    HANDLE h = CreateFileA(g_logPath.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, buf, (DWORD)strlen(buf), &wr, nullptr);
        CloseHandle(h);
    }
    OutputDebugStringA(buf);
    LeaveCriticalSection(&g_logCs);
}

static uintptr_t g_base = 0;
uintptr_t base() {
    if (!g_base) g_base = (uintptr_t)GetModuleHandleA("Weixin.dll");
    return g_base;
}

bool rpm(uintptr_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    return addr && ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, n, &got) &&
           got == n;
}
uint64_t rpmPtr(uintptr_t addr) {
    uint64_t v = 0;
    return rpm(addr, &v, 8) ? v : 0;
}
uint32_t rpmU32(uintptr_t addr) {
    uint32_t v = 0;
    return rpm(addr, &v, 4) ? v : 0;
}
bool rpmCStrEq(uintptr_t p, const char* s) {
    char c;
    for (; *s; ++s, ++p) {
        if (!rpm(p, &c, 1) || c != *s) return false;
    }
    return rpm(p, &c, 1) && c == '\0';
}
std::string rpmCStr(uintptr_t p, size_t maxLen) {
    std::string s;
    char c;
    for (size_t i = 0; i < maxLen; ++i) {
        if (!rpm(p + i, &c, 1) || c == '\0') break;
        s.push_back(c);
    }
    return s;
}

static Sections g_sec = {0, 0, 0, 0, 0, 0};
static bool g_secDone = false;

const Sections& sections() {
    if (g_secDone) return g_sec;
    HMODULE h = GetModuleHandleA("Weixin.dll");
    if (h) {
        auto dos = (IMAGE_DOS_HEADER*)h;
        auto nt = (IMAGE_NT_HEADERS64*)((uintptr_t)h + dos->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            uintptr_t b = (uintptr_t)h + sec[i].VirtualAddress;
            uintptr_t e = b + sec[i].Misc.VirtualSize;
            if (!memcmp(sec[i].Name, ".text", 5)) {
                g_sec.textBase = b;
                g_sec.textEnd = e;
            } else if (!memcmp(sec[i].Name, ".rdata", 6)) {
                g_sec.rdataBase = b;
                g_sec.rdataEnd = e;
            } else if (!memcmp(sec[i].Name, ".data", 5)) {
                g_sec.dataBase = b;
                g_sec.dataEnd = e;
            }
        }
    }
    g_secDone = true;
    return g_sec;
}

using FnAlloc = void* (*)(uint64_t);

static uint64_t calcCap(uint64_t len) {
    uint64_t c = (len + 1 + 0xF) & ~0xFULL;
    return c - 1;
}

void setWx(void* dst, const std::string& s) {
    WeixinString* w = (WeixinString*)dst;
    memset(w, 0, sizeof(WeixinString));
    w->length = s.size();
    if (s.size() < 16) {
        memcpy(w->inline_buf, s.data(), s.size());
        w->cap = 0xF;
    } else {
        char* buf = (char*)fn<FnAlloc>(wxoff::WX_ALLOC)(s.size() + 1);
        if (!buf) {
            w->cap = 0xF;
            w->length = 0;
            return;
        }
        memcpy(buf, s.data(), s.size());
        buf[s.size()] = 0;
        w->heap_buf = buf;
        w->cap = calcCap(s.size());
    }
}

uint64_t imageSize() {
    if (!base()) return 0;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)base(), &mi, sizeof mi)) return 0;
    return (uint64_t)mi.SizeOfImage;
}

bool versionMatches() {
    static int cached = -1;
    if (cached < 0) cached = (imageSize() == wxoff::MODULE_SIZE) ? 1 : 0;
    return cached != 0;
}

} // namespace platform
} // namespace wxhook
