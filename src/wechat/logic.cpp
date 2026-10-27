// logic.cpp -- logic-thread task queue driven by the TLS getter hook
#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/logic.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace wxhook {

namespace {

void* ctxFieldAddr() {
    if (!platform::base()) return nullptr;
    uint32_t idx = *(uint32_t*)(platform::base() + wxoff::TLS_INDEX_GLB);
    char* teb = (char*)NtCurrentTeb();
    void** tlsarr = *(void***)(teb + 0x58);
    if (!tlsarr) return nullptr;
    void* slot = tlsarr[idx];
    if (!slot) return nullptr;
    return (char*)slot + wxoff::TLS_CTX_FIELD;
}

struct Task {
    std::function<bool()> fn;
    HANDLE done = nullptr;
    volatile int result = 0;
    volatile int taken = 0;
};

std::vector<Task*> g_queue;
CRITICAL_SECTION g_qcs;
bool g_qInit = false;
volatile LONG g_taskBusy = 0;
volatile long g_hits = 0;
volatile DWORD g_logicTid = 0;

void ensureInit() {
    if (!g_qInit) {
        InitializeCriticalSection(&g_qcs);
        g_qInit = true;
    }
}

} // namespace

long getterHookHits() { return g_hits; }

void getterHookHit() {
    g_hits++;
    if (!g_logicTid) g_logicTid = GetCurrentThreadId();
}

unsigned long logicThreadId() { return g_logicTid; }

void drainTaskQueue() {
    if (g_queue.empty()) return;
    if (InterlockedCompareExchange(&g_taskBusy, 1, 0) != 0) return;
    void* cf = ctxFieldAddr();
    if (!cf || !*(void**)cf) {
        InterlockedExchange(&g_taskBusy, 0);
        return;
    }
    Task* t = nullptr;
    EnterCriticalSection(&g_qcs);
    if (!g_queue.empty()) {
        t = g_queue.front();
        g_queue.erase(g_queue.begin());
        t->taken = 1;
    }
    LeaveCriticalSection(&g_qcs);
    if (t) {
        t->result = t->fn ? (t->fn() ? 1 : 0) : 0;
        SetEvent(t->done);
    }
    InterlockedExchange(&g_taskBusy, 0);
}

static int runOnLogicImpl(std::function<bool()> fn, unsigned timeoutMs) {
    ensureInit();
    Task* t = new Task();
    t->fn = std::move(fn);
    t->done = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    EnterCriticalSection(&g_qcs);
    g_queue.push_back(t);
    LeaveCriticalSection(&g_qcs);

    DWORD wr = WaitForSingleObject(t->done, timeoutMs);
    int ret;
    if (wr == WAIT_OBJECT_0) {
        ret = t->result ? 1 : 0;
        CloseHandle(t->done);
        delete t;
    } else {
        EnterCriticalSection(&g_qcs);
        bool taken = t->taken != 0;
        if (!taken)
            for (size_t i = 0; i < g_queue.size(); ++i)
                if (g_queue[i] == t) {
                    g_queue.erase(g_queue.begin() + i);
                    break;
                }
        LeaveCriticalSection(&g_qcs);
        if (!taken) {
            CloseHandle(t->done);
            delete t;
            ret = 0;
        } else if (WaitForSingleObject(t->done, 5000) == WAIT_OBJECT_0) {
            ret = t->result ? 1 : 0;
            CloseHandle(t->done);
            delete t;
        } else {
            platform::logf("[marshal] task stuck, leaking Task");
            ret = -1;
        }
    }
    return ret;
}

bool runOnLogicThread(std::function<bool()> task, unsigned timeoutMs) {
    return runOnLogicImpl(std::move(task), timeoutMs) == 1;
}

} // namespace wxhook
