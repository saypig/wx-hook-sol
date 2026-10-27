// crash.cpp -- rescue the two known downstream AVs after a synthetic image/video send
#include <windows.h>
#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/crash.h"

namespace wxhook {

namespace {
volatile long g_saves = 0;

LONG WINAPI ImgCrashVeh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != 0xC0000005) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t b = platform::base();
    if (!b) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t rip = (uintptr_t)ep->ContextRecord->Rip;
    uintptr_t fa = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];

    uintptr_t fn = b + wxoff::IMG_BAD_DTOR;
    if (rip >= fn && rip < fn + 0xCD) {
        InterlockedIncrement(&g_saves);
        platform::logf("[veh] image dtor AV #%ld", g_saves);
        ep->ContextRecord->Rip = b + wxoff::IMG_BAD_DTOR_TAIL;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    fn = b + wxoff::IMG_BAD_MEMCPY;
    if (rip >= fn && rip < fn + 0x66D) {
        uintptr_t rdx = ep->ContextRecord->Rdx;
        uintptr_t r8 = ep->ContextRecord->R8;
        if (fa >= rdx && fa < rdx + r8) {
            InterlockedIncrement(&g_saves);
            platform::logf("[veh] image memcpy AV #%ld", g_saves);
            ep->ContextRecord->Rax = ep->ContextRecord->Rcx;
            ep->ContextRecord->Rip = b + wxoff::IMG_BAD_MEMCPY_RET;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

bool imgveh_install() {
    void* h = AddVectoredExceptionHandler(1, ImgCrashVeh);
    if (!h) {
        platform::logf("[veh] install failed");
        return false;
    }
    platform::logf("[veh] installed");
    return true;
}

} // namespace wxhook
