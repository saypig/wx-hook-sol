// dllmain.cpp -- module bootstrap
#include <windows.h>
#include "platform/runtime.h"
#include "platform/offsets.h"
#include "transport/server.h"
#include "wechat/crash.h"
#include "wechat/media.h"

using namespace wxhook;

static DWORD WINAPI initThread(LPVOID) {
    for (int i = 0; i < 200 && !platform::base(); ++i) Sleep(50);
    if (!platform::base()) {
        platform::logf("[init] Weixin.dll not loaded");
        return 1;
    }
    platform::logf("[init] wxhook target=%s base=%p", wxoff::WX_VER, (void*)platform::base());
    platform::sections();
    platform::setLogPort(WXHOOK_PORT);
    platform::logf("[init] port=%d pid=%lu", WXHOOK_PORT,
               (unsigned long)GetCurrentProcessId());

    if (!platform::versionMatches()) {
        platform::logf("[init] version mismatch: current 0x%llX, expected 0x%llX",
                   (unsigned long long)platform::imageSize(),
                   (unsigned long long)wxoff::MODULE_SIZE);
        server_serve(WXHOOK_PORT);
        return 0;
    }

    if (!imgsend_install())
        platform::logf("[init] imgsend_install failed");
    if (!imgveh_install())
        platform::logf("[init] imgveh_install failed");

    server_serve(WXHOOK_PORT);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        static HANDLE s_beacon = nullptr;
        char nm[64];
        _snprintf(nm, sizeof nm, "Local\\wxhook_inst_%lu",
                  (unsigned long)GetCurrentProcessId());
        s_beacon = CreateEventA(nullptr, TRUE, FALSE, nm);
        platform::logf("[init] wxhook.dll attached pid=%lu (beacon %s)",
                   GetCurrentProcessId(), s_beacon ? "OK" : "failed");
        CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
