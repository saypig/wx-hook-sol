// account.cpp -- find the win32 sqlite3 VFS and derive the current wxid
// from the open database handles. Read-only; never calls into sqlite3.
#include "wechat/account.h"
#include "platform/runtime.h"
#include "platform/offsets.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace wxhook {
namespace account {

using platform::rpmCStr;
using platform::rpmCStrEq;
using platform::rpmPtr;
using platform::rpmU32;

static uintptr_t g_vfs = 0;
static std::vector<std::pair<std::string, uintptr_t>> g_cache;
static DWORD g_cacheMs = 0;

static std::string basename_(const std::string& p) {
    size_t s = p.find_last_of("\\/");
    return (s == std::string::npos) ? p : p.substr(s + 1);
}

static uintptr_t findVfs() {
    const auto& sec = platform::sections();
    for (uintptr_t a = sec.dataBase; a && a + 0x30 <= sec.dataEnd; a += 8) {
        int iv = *(int*)a;
        if (iv < 1 || iv > 3) continue;
        if (*(int*)(a + wxoff::VFS_SZOSFILE) != 0x60) continue;
        if (*(int*)(a + wxoff::VFS_MXPATHNAME) != 0x410) continue;
        uintptr_t z = *(uintptr_t*)(a + wxoff::VFS_ZNAME);
        if (rpmCStrEq(z, "win32")) return a;
    }
    return 0;
}

static std::string handleFile(uintptr_t H) {
    uintptr_t aDb = rpmPtr(H + wxoff::DB_ADB);
    if (!aDb) return {};
    uintptr_t bt = rpmPtr(aDb + wxoff::DBREC_PBT);
    if (!bt) return {};
    uintptr_t bts = rpmPtr(bt + wxoff::BTREE_PBT);
    if (!bts) return {};
    uintptr_t pg = rpmPtr(bts + wxoff::BTSH_PAGER);
    if (!pg) return {};
    for (int off = 0xC0; off <= 0x120; off += 8) {
        uintptr_t s = rpmPtr(pg + off);
        if (!s) continue;
        std::string str = rpmCStr(s);
        if (str.size() >= 5 && str.find(".db") != std::string::npos &&
            (str.find(":\\") != std::string::npos || str.find('/') != std::string::npos))
            return str;
    }
    return {};
}

static void scanHandles() {
    g_cache.clear();
    if (!g_vfs) return;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t a = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t e = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;
    std::vector<uint8_t> buf;
    while (a < e) {
        if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi))) break;
        uintptr_t rb = (uintptr_t)mbi.BaseAddress;
        size_t rs = mbi.RegionSize;
        bool wr = (mbi.State == MEM_COMMIT) && (mbi.Type == MEM_PRIVATE) &&
                  (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
                  !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        a = rb + rs;
        if (!wr) continue;
        const size_t CH = 8u * 1024 * 1024;
        for (uintptr_t bs = rb; bs < rb + rs; bs += CH) {
            size_t want = (size_t)((rb + rs - bs) < CH ? (rb + rs - bs) : CH);
            if (buf.size() < want) buf.resize(want);
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)bs, buf.data(), want, &got) ||
                got < 8)
                continue;
            for (size_t off = 0; off + 8 <= got; off += 8) {
                if (*(uintptr_t*)(buf.data() + off) != g_vfs) continue;
                uintptr_t H = bs + off;
                int nDb = (int)rpmU32(H + wxoff::DB_NDB);
                if (nDb < 1 || nDb > 16) continue;
                if (!rpmPtr(H + wxoff::DB_ADB)) continue;
                std::string f = handleFile(H);
                if (f.empty()) continue;
                std::string bn = basename_(f);
                bool dup = false;
                for (auto& kv : g_cache)
                    if (kv.first == bn) {
                        kv.second = H;
                        dup = true;
                        break;
                    }
                if (!dup) g_cache.push_back({bn, H});
            }
        }
    }
}

static void refresh() {
    scanHandles();
    g_cacheMs = GetTickCount();
}

void init() {
    g_vfs = findVfs();
    platform::logf("[account] win32 VFS=%p", (void*)g_vfs);
}

static bool isHex4Suffix(const std::string& seg, size_t& cut) {
    size_t u = seg.find_last_of('_');
    if (u == std::string::npos || u == 0) return false;
    if (seg.size() - u - 1 != 4) return false;
    for (size_t i = u + 1; i < seg.size(); ++i) {
        char c = seg[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    cut = u;
    return true;
}

std::string selfWxid() {
    if (g_cache.empty() || GetTickCount() - g_cacheMs > 30000) refresh();
    static const char* keys[] = {
        "xwechat_files\\", "xwechat_files/", "WeChat Files\\", "WeChat Files/",
        "xwechat\\", "xwechat/"};
    for (auto& kv : g_cache) {
        std::string f = handleFile(kv.second);
        for (const char* k : keys) {
            size_t p = f.find(k);
            if (p == std::string::npos) continue;
            size_t s = p + strlen(k);
            size_t e = f.find_first_of("\\/", s);
            std::string seg = (e == std::string::npos) ? f.substr(s) : f.substr(s, e - s);
            if (seg.empty() || seg == "all_users" || seg == "db_storage") continue;
            size_t cut;
            if (seg.rfind("wxid_", 0) == 0 && isHex4Suffix(seg, cut)) seg = seg.substr(0, cut);
            return seg;
        }
    }
    return "";
}

} // namespace account
} // namespace wxhook
