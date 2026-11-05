// db.cpp -- run read-only SQL through the client's own sqlite3 handles
//
// The handle is owned by WeChat: it can be closed between our validity check
// and sqlite3_exec (login/logout). A small VEH shield turns that one AV into a
// failed query instead of a crash. Only active while our exec is running.
#include <windows.h>
#include <csetjmp>
#include <string>

#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/account.h"
#include "wechat/db.h"

namespace wxhook {
namespace db {

using ExecFn = int (*)(void*, const char*, int (*)(void*, int, char**, char**), void*,
                       char**);

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_cs;
static volatile LONG g_guard = 0;
static volatile DWORD g_guardTid = 0;
static jmp_buf g_jmp;

static LONG CALLBACK dbVeh(PEXCEPTION_POINTERS ep) {
    if (!g_guard || GetCurrentThreadId() != g_guardTid)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP ||
        code == 0x40010006u)  // OutputDebugString
        return EXCEPTION_CONTINUE_SEARCH;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case 0xC0000028u:  // STATUS_BAD_STACK
    case 0xC0000374u:  // STATUS_HEAP_CORRUPTION
    case 0xC0000409u:  // STATUS_STACK_BUFFER_OVERRUN
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    longjmp(g_jmp, 1);
}

static BOOL CALLBACK initShield(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_cs);
    AddVectoredExceptionHandler(1, dbVeh);
    return TRUE;
}

static int execCb(void* arg, int nCol, char** vals, char** names) {
    Rows* rows = (Rows*)arg;
    Row row;
    row.reserve((size_t)nCol);
    for (int i = 0; i < nCol; ++i)
        row.push_back({names && names[i] ? names[i] : "?",
                       vals && vals[i] ? vals[i] : ""});
    rows->push_back(std::move(row));
    return 0;
}

bool query(const std::string& dbName, const std::string& sql, Rows* out,
           std::string* err) {
    if (!out) return false;
    out->clear();
    if (!platform::base()) {
        if (err) *err = "weixin not loaded";
        return false;
    }
    uintptr_t h = account::handleFor(dbName);
    if (!h) {
        if (err) *err = "db not open: " + dbName;
        return false;
    }

    InitOnceExecuteOnce(&g_once, initShield, nullptr, nullptr);
    ExecFn exec = platform::fn<ExecFn>(wxoff::SQLITE3_EXEC);
    char* em = nullptr;
    int rc;

    // g_jmp is process-wide, so exec and its shield must not overlap.
    EnterCriticalSection(&g_cs);
    if (setjmp(g_jmp) != 0) {
        InterlockedExchange(&g_guard, 0);
        LeaveCriticalSection(&g_cs);
        if (err) *err = "db handle lost";
        return false;
    }
    g_guardTid = GetCurrentThreadId();
    InterlockedExchange(&g_guard, 1);
    rc = exec((void*)h, sql.c_str(), execCb, out, &em);
    InterlockedExchange(&g_guard, 0);
    LeaveCriticalSection(&g_cs);

    if (rc != 0) {
        if (err) *err = em ? em : ("sqlite rc=" + std::to_string(rc));
        return false;
    }
    return true;
}

} // namespace db
} // namespace wxhook
