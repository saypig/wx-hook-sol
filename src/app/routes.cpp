// routes.cpp -- route registration and request dispatch
#include <windows.h>
#include <exception>
#include <string>

#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/account.h"
#include "transport/http.h"
#include "features/routes.h"

using namespace wxhook;

static http::Router* g_router = nullptr;

extern "C" int wxhook_init(void) {
    platform::sections();
    account::init();
    if (!g_router) {
        g_router = new http::Router();
        features::register_status(*g_router);
        if (!platform::versionMatches()) {
            platform::logf("[core] version mismatch: only /status is available");
            return 0;
        }
        features::register_send(*g_router);
        features::register_pat(*g_router);
    }
    platform::logf("[core] wxhook_init done, base=%p", (void*)platform::base());
    return 0;
}

extern "C" void wxhook_dispatch(const char* method, const char* path,
                                const char* body, int bodyLen,
                                void (*emit)(void* u, const char* data, int len),
                                void* u) {
    http::Req req;
    req.method = method ? method : "";
    req.path = path ? path : "";
    if (body && bodyLen > 0) req.body.assign(body, (size_t)bodyLen);

    std::string resp;
    if (!platform::versionMatches() && req.path != "/status") {
        resp = http::err("version mismatch: expected " + std::string(wxoff::WX_VER));
    } else if (features::loginGateBlocks(req.path)) {
        resp = http::err("not logged in");
    } else {
        try {
            resp = g_router ? g_router->dispatch(req) : http::err("router null");
        } catch (const std::exception& e) {
            resp = http::err(std::string("handler exception: ") + e.what());
        } catch (...) {
            resp = http::err("handler unknown exception");
        }
    }
    if (emit) emit(u, resp.data(), (int)resp.size());
}
