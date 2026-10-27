// send.cpp —— 最小发送:文本 / 图片 / 视频
//
// 只保留三条路由:
//   POST /SendTextMsg  {"to_wxid":"...", "content":"..."}
//   POST /SendImgMsg   {"to_wxid":"...", "path":"..."}
//   POST /SendVideoMsg {"to_wxid":"...", "path":"..."}
//
// 文本走 CREATE_MSG_OBJ + SEND_ONE(flag=1 联网);
// 图片/视频走 marshal/imgsend.cpp 的内容对象通道,由客户端自己读文件、编码、上传 CDN。
#include <windows.h>
#include <cstring>
#include <string>

#include "features/routes.h"
#include "platform/runtime.h"
#include "platform/offsets.h"
#include "wechat/account.h"
#include "wechat/logic.h"
#include "wechat/media.h"

using namespace wxhook;
using http::json;

using FnCreate = void (*)(void*);
using FnGet1   = void (*)(void*);
using FnGet2   = void (*)(void*, void*);
using FnSend   = void (*)(void*, void*, void*, uint64_t);

namespace {

bool doSend(const std::string& wxid, const std::string& content, int32_t msgType,
            int32_t subtype = 0) {
    if (account::selfWxid().empty()) {
        platform::logf("[send] 未登录,拒绝发送(客户端发送服务未初始化,SEND_ONE 会崩)");
        return false;
    }
    platform::logf("[send] doSend tid=%lu wxid=%s type=%d len=%zu",
               GetCurrentThreadId(), wxid.c_str(), (int)msgType, content.size());

    void* sp[2] = { nullptr, nullptr };
    platform::fn<FnCreate>(wxoff::CREATE_MSG_OBJ)(sp);
    void* obj = sp[0], *ctrl = sp[1];
    if (!obj) {
        platform::logf("[send] CreateSendMsgObject 失败");
        return false;
    }
    platform::setWx((char*)obj + wxoff::MSG_RECEIVER, wxid);
    platform::setWx((char*)obj + wxoff::MSG_CONTENT, content);
    *(int32_t*)((char*)obj + wxoff::MSG_TYPE_9C) = 1;
    *(int32_t*)((char*)obj + wxoff::MSG_TYPE_118) = msgType;
    if (msgType == wxoff::MSGTYPE_APPMSG)
        *(int32_t*)((char*)obj + wxoff::MSG_SUBTYPE_11C) = subtype;

    uint64_t root[2] = {0}, sub[2] = {0}, svc[2] = {0};
    platform::fn<FnGet1>(wxoff::GET_ROOT)(root);
    platform::fn<FnGet2>(wxoff::GET_SUB)((void*)root[0], sub);
    platform::fn<FnGet2>(wxoff::GET_SVC)((void*)sub[0], svc);
    void* service = (void*)svc[0];
    if (!service) {
        platform::logf("[send] service null");
        return false;
    }

    void* a3[2] = { obj, ctrl };
    char out[0x60];
    memset(out, 0, sizeof(out));
    platform::fn<FnSend>(wxoff::SEND_ONE)(service, out, a3, 1);
    platform::logf("[send] SEND_ONE(flag=1,type=%d) OK", (int)msgType);
    return true;
}

} // namespace

namespace wxhook {
namespace features {

void register_send(http::Router& r) {
    r.post(ROUTE("/SendTextMsg"), [](const http::Req& req) -> std::string {
        if (const char* d = features::sendDeniedIfLoggedOut()) return d;
        json b = req.j();
        std::string wxid = b.value("to_wxid", std::string());
        std::string msg  = b.value("content", std::string());
        if (wxid.empty() || msg.empty())
            return http::raw(json{{"ret", -1}, {"retmsg", "missing to_wxid/content"}});
        bool ok = runOnLogicThread(
            [=]() { return doSend(wxid, msg, wxoff::MSGTYPE_TEXT, 0); });
        return http::raw(json{{"ret", ok ? 0 : -1},
                              {"retmsg", ok ? "success" : "send failed/timeout"}});
    });

    // 图片与视频共用同一张串行闸:客户端上传协程收尾需要数秒,并发入列会取不到
    // 上下文而崩。最小间隔 3s,与已验证的发图路径一致。
    static volatile LONG imgGate = 0;
    static volatile ULONGLONG imgLastTick = 0;

    auto sendImageAs = [](const http::Req& req) -> std::string {
        if (const char* d = features::sendDeniedIfLoggedOut()) return d;
        json b = req.j();
        std::string wxid = b.value("to_wxid", std::string());
        std::string path = b.value("path", std::string());
        if (wxid.empty() || path.empty())
            return http::raw(json{{"ret", -1}, {"retmsg", "missing to_wxid/path"}});
        int wn1 = MultiByteToWideChar(CP_UTF8, 0, wxid.c_str(), -1, nullptr, 0);
        int wn2 = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wn1 <= 0 || wn1 > 128 || wn2 <= 0 || wn2 > 1024)
            return http::raw(json{{"ret", -1}, {"retmsg", "bad wxid/path"}});
        wchar_t wwxid[128], wpath[1024];
        MultiByteToWideChar(CP_UTF8, 0, wxid.c_str(), -1, wwxid, wn1);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, wn2);

        while (InterlockedCompareExchange(&imgGate, 1, 0) != 0) Sleep(50);
        ULONGLONG last = imgLastTick, now = GetTickCount64();
        if (last && now < last + 3000) Sleep((DWORD)(last + 3000 - now));
        bool ok = imgSend(wwxid, wpath, 20000);
        imgLastTick = GetTickCount64();
        InterlockedExchange(&imgGate, 0);
        return http::raw(json{{"ret", ok ? 0 : -1},
                              {"retmsg", ok ? "success" : "send failed/timeout"}});
    };
    r.post(ROUTE("/SendImgMsg"), sendImageAs);

    r.post(ROUTE("/SendVideoMsg"), [](const http::Req& req) -> std::string {
        if (const char* d = features::sendDeniedIfLoggedOut()) return d;
        json b = req.j();
        std::string wxid = b.value("to_wxid", std::string());
        std::string path = b.value("path", std::string());
        if (wxid.empty() || path.empty())
            return http::raw(json{{"ret", -1}, {"retmsg", "missing to_wxid/path"}});
        int wn1 = MultiByteToWideChar(CP_UTF8, 0, wxid.c_str(), -1, nullptr, 0);
        int wn2 = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wn1 <= 0 || wn1 > 128 || wn2 <= 0 || wn2 > 1024)
            return http::raw(json{{"ret", -1}, {"retmsg", "bad wxid/path"}});
        wchar_t wwxid[128], wpath[1024];
        MultiByteToWideChar(CP_UTF8, 0, wxid.c_str(), -1, wwxid, wn1);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, wn2);

        while (InterlockedCompareExchange(&imgGate, 1, 0) != 0) Sleep(50);
        ULONGLONG last = imgLastTick, now = GetTickCount64();
        if (last && now < last + 3000) Sleep((DWORD)(last + 3000 - now));
        bool ok = mediaSend(wwxid, wpath, 30000, BSM_VIDEO);
        imgLastTick = GetTickCount64();
        InterlockedExchange(&imgGate, 0);
        return http::raw(json{{"ret", ok ? 0 : -1},
                              {"retmsg", ok ? "success" : "send failed/timeout"}});
    });
}

} // namespace features
} // namespace wxhook
