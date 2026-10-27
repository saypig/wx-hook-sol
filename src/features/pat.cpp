// pat.cpp —— 拍一拍(/SendPatMsg)
//
// 现造拍一拍请求对象,调客户端自己的 sendpat CGI 执行体:
//   GET_ROOT → GET_SUB → PAT_SVC_GET 取会话 service
//   SEND_PAT_CORE(service, out, 会话串, 请求对象)
// 不需要模板,也不需要先手动拍一次。
#include "features/routes.h"
#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/account.h"
#include "wechat/logic.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>

using namespace wxhook;
using http::json;

namespace {

constexpr int kA4Sz = 0x440;
constexpr int kA4CtorRva = 0xA5A150;   // [4.1.15.13] 拍一拍请求对象构造函数

// 写 std::string:短串内联,长串走微信分配器(可能被微信 free)。
void writeSt(uint8_t* base, int off, const std::string& v) {
    uint64_t l = v.size();
    if (l < 16) {
        memset(base + off, 0, 16);
        memcpy(base + off, v.data(), (size_t)l);
        *(uint64_t*)(base + off + 0x18) = 15;
    } else {
        char* p = platform::fn<char* (*)(size_t)>(wxoff::WX_ALLOC)((size_t)l + 1);
        if (!p) return;
        memcpy(p, v.data(), (size_t)l);
        p[l] = 0;
        *(char**)(base + off) = p;
        *(uint64_t*)(base + off + 0x18) = l + 0x10;
    }
    *(uint64_t*)(base + off + 0x10) = l;
}

void buildA4Scratch(const std::string& chat, const std::string& target, uint8_t* out) {
    memset(out, 0, kA4Sz);
    platform::fn<void (*)(void*)>(kA4CtorRva)(out);
    *(uint64_t*)(out + 0x08) = 0x7D000000031ULL;
    *(uint64_t*)(out + 0x90) = 0x3B100000000ULL;
    const std::string self = account::selfWxid();
    const std::string tgt  = target.empty() ? chat : target;
    writeSt(out, 0x10, chat);
    writeSt(out, 0x30, self);
    writeSt(out, 0x50, tgt);
    writeSt(out, 0x70, chat);
}

} // namespace

namespace wxhook {
namespace features {

void register_pat(http::Router& r) {
    r.post(ROUTE("/SendPatMsg"), [](const http::Req& req) -> std::string {
        if (const char* d = features::sendDeniedIfLoggedOut()) return d;
        json b = req.j();
        std::string chat   = b.value("to_wxid", std::string());
        std::string target = b.value("member_wxid", std::string());
        if (chat.empty())
            return http::raw(json{{"ret", -1}, {"retmsg", "missing to_wxid"}});

        // 静态缓冲:marshal 超时后逻辑线程仍可能在使用它(与原实现一致)。
        static uint8_t a4[kA4Sz];
        static uint8_t out[0x100];
        buildA4Scratch(chat, target, a4);
        memset(out, 0, sizeof(out));

        const std::string t = target.empty() ? chat : target;
        platform::logf("[pat] chat=%s target=%s", chat.c_str(), t.c_str());
        bool ok = runOnLogicThread([&]() {
            uint64_t root[2] = {0}, sub[2] = {0}, svc[2] = {0};
            platform::fn<void (*)(void*)>(wxoff::GET_ROOT)(root);
            platform::fn<void (*)(void*, void*)>(wxoff::GET_SUB)((void*)root[0], sub);
            platform::fn<void (*)(void*, void*)>(wxoff::PAT_SVC_GET)((void*)sub[0], svc);
            if (!svc[0]) {
                platform::logf("[pat] service null");
                return false;
            }
            uint8_t a3[0x20];
            memset(a3, 0, sizeof(a3));
            writeSt(a3, 0, chat);
            platform::fn<void (*)(void*, void*, void*, void*)>(wxoff::SEND_PAT_CORE)
                ((void*)svc[0], out, a3, a4);
            platform::logf("[pat] SEND_PAT_CORE 已调用");
            return true;
        });
        return http::raw(json{{"ret", ok ? 0 : -1},
                              {"retmsg", ok ? "success" : "pat failed/timeout"}});
    });
}

} // namespace features
} // namespace wxhook
