// status.cpp -- /status and the login gate
#include "features/routes.h"
#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/account.h"
#include "wechat/logic.h"
#include <string>

namespace wxhook {
namespace features {

const char* sendDeniedIfLoggedOut() {
    if (!account::selfWxid().empty()) return nullptr;
    return "{\"ret\":-1,\"retmsg\":\"not logged in\"}";
}

bool loginGateBlocks(const std::string& path) {
    if (!account::selfWxid().empty()) return false;
    return path != "/status";
}

void register_status(http::Router& r) {
    auto status = [](const http::Req&) -> std::string {
        const std::string self = account::selfWxid();
        http::json o;
        o["ret"] = 0;
        o["self"] = self;
        o["wxid"] = self;
        o["IsLogin"] = !self.empty();
        o["base"] = (unsigned long long)platform::base();
        o["version"] = wxoff::WX_VER;
        o["sizeOfImage"] = (unsigned long long)platform::imageSize();
        o["hookHits"] = (long long)getterHookHits();
        return http::raw(o);
    };
    r.get(ROUTE("/status"), status);
    r.post(ROUTE("/status"), status);
}

} // namespace features
} // namespace wxhook
