// media.cpp -- image/video sender driven by the TLS getter hook
#include <windows.h>
#include <objbase.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

#include "MinHook.h"
#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/media.h"
#include "wechat/logic.h"

namespace wxhook {

using PrepSendFn = void* (__fastcall*)(void*, void*, void*, unsigned int);
using MakeContentFn = void* (__fastcall*)(void*);
using AllocFn = void* (__fastcall*)(size_t);
using GetCtxFn = void* (__fastcall*)(void* out16);

enum { OFF_WXID = 0xC0, OFF_PATH = 0x130, OFF_NAME = 0x1B8, OFF_SIZE = 0x1D8, OFF_ID = 0x700 };
enum { KIND_IMAGE = 0, KIND_VIDEO = 1 };

template <class CH, size_t SSO>
static void set_str(uint8_t* field, const CH* val, size_t len) {
    size_t* pSize = (size_t*)(field + 0x10);
    size_t* pCap = (size_t*)(field + 0x18);
    if (len < SSO) {
        memcpy(field, val, len * sizeof(CH));
        ((CH*)field)[len] = 0;
        *pSize = len;
        *pCap = SSO - 1;
    } else {
        CH* buf = (CH*)((AllocFn)(platform::base() + wxoff::WX_ALLOC))((len + 1) * sizeof(CH));
        memcpy(buf, val, len * sizeof(CH));
        buf[len] = 0;
        *(CH**)field = buf;
        *pSize = len;
        *pCap = len;
    }
}

static std::string genGuid() {
    GUID g;
    CoCreateGuid(&g);
    char b[40];
    snprintf(b, sizeof(b), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned long)g.Data1, (unsigned)g.Data2, (unsigned)g.Data3, g.Data4[0],
             g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
             g.Data4[7]);
    return std::string(b, 36);
}

static bool CurCtx(uint64_t* obj) {
    uint32_t idx = *(uint32_t*)(platform::base() + wxoff::TLS_INDEX_GLB);
    uint64_t** slotsp = (uint64_t**)((char*)NtCurrentTeb() + 0x58);
    uint64_t* slots = *slotsp;
    if (!slots) return false;
    uint64_t blk = slots[idx];
    if (!blk) return false;
    uint64_t v = *(uint64_t*)(blk + 0x1F0);
    if (!v) return false;
    if (obj) *obj = v;
    return true;
}

static void* findService() {
    uint64_t root[2] = {0, 0}, sub[2] = {0, 0}, svc[2] = {0, 0};
    ((void (*)(void*))(platform::base() + wxoff::GET_ROOT))((void*)root);
    if (!root[0]) return nullptr;
    ((void (*)(void*, void*))(platform::base() + wxoff::GET_SUB))((void*)root[0], (void*)sub);
    if (!sub[0]) return nullptr;
    ((void (*)(void*, void*))(platform::base() + wxoff::GET_SVC))((void*)sub[0], (void*)svc);
    return (void*)svc[0];
}

static bool do_send(const wchar_t* wxidW, const wchar_t* pathW, int kind) {
    if (kind != KIND_IMAGE && kind != KIND_VIDEO) return false;

    static void* s_svc = nullptr;
    if (!s_svc) s_svc = findService();
    void* mgr = s_svc;
    if (!mgr) {
        platform::logf("[img] service chain unavailable");
        return false;
    }

    char wxidA[128];
    WideCharToMultiByte(CP_UTF8, 0, wxidW, -1, wxidA, sizeof wxidA, 0, 0);
    std::string wxid(wxidA);
    std::wstring path(pathW);

    std::wstring nameW = path.substr(path.find_last_of(L"\\/") + 1);
    std::string nameU;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), (int)nameW.size(), nullptr, 0,
                                    0, 0);
        if (n > 0) {
            nameU.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), (int)nameW.size(), &nameU[0], n, 0,
                                0);
        }
    }

    uint64_t fsize = 0;
    {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa))
            fsize = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    }
    if (!fsize) {
        platform::logf("[img] file not found or empty");
        return false;
    }

    const uint64_t factory =
        (kind == KIND_VIDEO) ? wxoff::MAKE_VIDEO_CONTENT : wxoff::MAKE_IMG_CONTENT;
    const uint32_t mtype = (kind == KIND_VIDEO) ? 43 : 3;

    void* sp[2] = {nullptr, nullptr};
    ((MakeContentFn)(platform::base() + factory))(sp);
    if (!sp[1]) {
        platform::logf("[img] make content failed");
        return false;
    }
    uint8_t* base = (uint8_t*)sp[1];

    set_str<char, 16>(base + OFF_WXID, wxid.c_str(), wxid.size());
    set_str<wchar_t, 8>(base + OFF_PATH, path.c_str(), path.size());
    set_str<char, 16>(base + OFF_NAME, nameU.c_str(), nameU.size());
    *(uint32_t*)(base + OFF_SIZE) = (uint32_t)fsize;
    {
        std::string id = genGuid();
        set_str<char, 16>(base + OFF_ID, id.c_str(), id.size());
    }
    *(uint32_t*)(base + 0xE0) = 1;
    *(uint32_t*)(base + 0x128) = mtype;
    if (kind == KIND_VIDEO) *(uint32_t*)(base + 0x118) = mtype;

    uint8_t* out = new uint8_t[0x100];
    memset(out, 0, 0x100);
    platform::logf("[img] PrepSend kind=%d wxid=%s size=%llu", kind, wxid.c_str(),
               (unsigned long long)fsize);
    ((PrepSendFn)(platform::base() + wxoff::SEND_ONE))(mgr, out, sp, 1);
    platform::logf("[img] returned OK");
    return true;
}

struct ImgReq {
    wchar_t wxid[128];
    wchar_t path[1024];
    int kind;
    HANDLE done;
    volatile LONG result;
};

static CRITICAL_SECTION g_reqCs;
static bool g_reqInit = false;
static ImgReq* g_pendingReq = nullptr;

static void reqEnsure() {
    if (!g_reqInit) {
        InitializeCriticalSection(&g_reqCs);
        g_reqInit = true;
    }
}

bool mediaSend(const wchar_t* wxidW, const wchar_t* pathW, unsigned timeoutMs, int kind) {
    if (kind != KIND_IMAGE && kind != KIND_VIDEO) return false;
    reqEnsure();
    ImgReq* req = new ImgReq();
    wcsncpy(req->wxid, wxidW, 127);
    req->wxid[127] = 0;
    wcsncpy(req->path, pathW, 1023);
    req->path[1023] = 0;
    req->kind = kind;
    req->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    req->result = 0;

    EnterCriticalSection(&g_reqCs);
    while (g_pendingReq) {
        LeaveCriticalSection(&g_reqCs);
        if (WaitForSingleObject(g_pendingReq->done, timeoutMs) != WAIT_OBJECT_0) {
            CloseHandle(req->done);
            delete req;
            return false;
        }
        EnterCriticalSection(&g_reqCs);
    }
    g_pendingReq = req;
    LeaveCriticalSection(&g_reqCs);

    bool ok = false;
    if (WaitForSingleObject(req->done, timeoutMs) == WAIT_OBJECT_0) {
        ok = req->result != 0;
        CloseHandle(req->done);
        delete req;
    } else {
        EnterCriticalSection(&g_reqCs);
        bool stillPending = (g_pendingReq == req);
        LeaveCriticalSection(&g_reqCs);
        if (!stillPending && WaitForSingleObject(req->done, 5000) == WAIT_OBJECT_0) {
            ok = req->result != 0;
            CloseHandle(req->done);
            delete req;
        } else {
            platform::logf("[img] request timeout");
            if (stillPending) {
                EnterCriticalSection(&g_reqCs);
                if (g_pendingReq == req) g_pendingReq = nullptr;
                LeaveCriticalSection(&g_reqCs);
                CloseHandle(req->done);
                delete req;
            }
            return false;
        }
    }
    return ok;
}

bool imgSend(const wchar_t* wxidW, const wchar_t* pathW, unsigned timeoutMs) {
    return mediaSend(wxidW, pathW, timeoutMs, KIND_IMAGE);
}

static GetCtxFn g_getCtxOrig = nullptr;

static void* __fastcall GetCtx_Hook(void* out16) {
    void* r = g_getCtxOrig(out16);
    getterHookHit();
    ImgReq* req = nullptr;
    EnterCriticalSection(&g_reqCs);
    if (g_pendingReq) {
        req = g_pendingReq;
        g_pendingReq = nullptr;
    }
    LeaveCriticalSection(&g_reqCs);
    if (req) {
        uint64_t ctxObj = 0;
        if (CurCtx(&ctxObj)) {
            platform::logf("[img] executing send in ctx=%llx tid=%lu",
                       (unsigned long long)ctxObj, GetCurrentThreadId());
            req->result = do_send(req->wxid, req->path, req->kind) ? 1 : 0;
        } else {
            platform::logf("[img] ctx window empty");
            req->result = 0;
        }
        SetEvent(req->done);
    }
    drainTaskQueue();
    return r;
}

bool imgsend_install() {
    reqEnsure();
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        platform::logf("[img] MH_Initialize failed st=%d", (int)st);
        return false;
    }
    const unsigned long long rva = wxoff::TLS_GETTER;
    if (rva == 0 || rva >= wxoff::MODULE_SIZE) {
        platform::logf("[img] bad getter rva");
        return false;
    }
    void* tgt = (void*)(platform::base() + rva);
    if (MH_CreateHook(tgt, (LPVOID)&GetCtx_Hook, (void**)&g_getCtxOrig) != MH_OK) {
        platform::logf("[img] MH_CreateHook failed");
        return false;
    }
    if (MH_EnableHook(tgt) != MH_OK) {
        platform::logf("[img] MH_EnableHook failed");
        return false;
    }
    platform::logf("[img] getter hooked");
    return true;
}

} // namespace wxhook
