// server.cpp -- local HTTP transport on 127.0.0.1:30002
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "platform/runtime.h"
#include "transport/http.h"
#include "transport/server.h"
#include "wechat/logic.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace wxhook {

static SRWLOCK g_coreLock;
static wxhook_dispatch_t g_dispatch = nullptr;
static int g_port = 0;
static bool g_wsaUp = false;

extern "C" int wxhook_init(void);
extern "C" void wxhook_dispatch(const char* method, const char* path,
                                const char* body, int bodyLen,
                                void (*emit)(void* u, const char* data, int len), void* u);

static void ensureWsa() {
    if (!g_wsaUp) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) == 0) g_wsaUp = true;
    }
}

static bool loadCore() {
    if (wxhook_init() != 0) {
        platform::logf("[init] wxhook_init failed");
        return false;
    }
    g_dispatch = &wxhook_dispatch;
    platform::logf("[init] wxhook loaded");
    return true;
}

static void sendAll(SOCKET s, const char* d, int len) {
    int off = 0;
    while (off < len) {
        int n = send(s, d + off, len - off, 0);
        if (n <= 0) break;
        off += n;
    }
}

static void respond(SOCKET s, const std::string& body) {
    char hdr[256];
    int n = _snprintf(hdr, sizeof(hdr),
                      "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                      "Content-Length: %d\r\nConnection: close\r\n\r\n",
                      (int)body.size());
    sendAll(s, hdr, n);
    sendAll(s, body.data(), (int)body.size());
}

static void emitCb(void* u, const char* d, int n) {
    ((std::string*)u)->append(d, (size_t)n);
}

static std::string handlePing() {
    char buf[320];
    _snprintf(buf, sizeof(buf),
              "{\"ret\":0,\"retmsg\":\"wxhook alive\",\"coreLoaded\":%d,"
              "\"hookHits\":%ld,\"port\":%d,\"pid\":%lu,\"logicTid\":%lu}",
              g_dispatch ? 1 : 0, getterHookHits(), g_port,
              (unsigned long)GetCurrentProcessId(), (unsigned long)logicThreadId());
    return buf;
}

static std::string handleReq(const std::string& method, const std::string& path,
                             const std::string& body) {
    if (http::fnv1a(path.data(), path.size()) == ROUTE("/ping")) return handlePing();

    if (!TryAcquireSRWLockShared(&g_coreLock))
        return http::err("core not loaded");
    std::string resp;
    if (g_dispatch)
        g_dispatch(method.c_str(), path.c_str(), body.c_str(), (int)body.size(), &emitCb, &resp);
    else
        resp = http::err("core not loaded");
    ReleaseSRWLockShared(&g_coreLock);
    if (resp.empty()) resp = http::err("empty core response");
    return resp;
}

static void handleConn(SOCKET c) {
    std::string reqbuf;
    char rb[4096];
    int hdrEnd = -1;
    while (hdrEnd < 0) {
        int n = recv(c, rb, sizeof(rb), 0);
        if (n <= 0) {
            closesocket(c);
            return;
        }
        reqbuf.append(rb, n);
        size_t pos = reqbuf.find("\r\n\r\n");
        if (pos != std::string::npos) hdrEnd = (int)(pos + 4);
        if (reqbuf.size() > (4u << 20)) break;
    }

    int clen = 0;
    {
        std::string low = reqbuf;
        for (auto& ch : low) ch = (char)tolower((unsigned char)ch);
        size_t cp = low.find("content-length:");
        if (cp != std::string::npos) clen = atoi(reqbuf.c_str() + cp + 15);
    }
    while ((int)reqbuf.size() < hdrEnd + clen) {
        int n = recv(c, rb, sizeof(rb), 0);
        if (n <= 0) break;
        reqbuf.append(rb, n);
    }

    std::string method, path;
    std::string body = reqbuf.substr(hdrEnd, clen);
    {
        size_t sp1 = reqbuf.find(' ');
        size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : reqbuf.find(' ', sp1 + 1);
        if (sp1 != std::string::npos) {
            method = reqbuf.substr(0, sp1);
            if (sp2 != std::string::npos) {
                path = reqbuf.substr(sp1 + 1, sp2 - sp1 - 1);
                size_t q = path.find('?');
                if (q != std::string::npos) path = path.substr(0, q);
            }
        }
    }

    std::string resp = http::envelope(handleReq(method, path, body));
    respond(c, resp);
    closesocket(c);
}

static SOCKET bindListenOn(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&yes, sizeof yes);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) {
        closesocket(s);
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return INVALID_SOCKET;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof yes);
        if (bind(s, (sockaddr*)&a, sizeof a) != 0) {
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
    if (listen(s, 16) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

void server_serve(int port) {
    if (port <= 0) port = WXHOOK_PORT;
    InitializeSRWLock(&g_coreLock);
    if (!loadCore()) platform::logf("[init] core load failed");
    ensureWsa();
    if (!g_wsaUp) {
        platform::logf("[init] WSAStartup failed");
        return;
    }
    SOCKET ls = bindListenOn(port);
    if (ls == INVALID_SOCKET) {
        platform::logf("[init] bind 127.0.0.1:%d failed err=%d", port, WSAGetLastError());
        return;
    }
    g_port = port;
    platform::logf("[init] listening 127.0.0.1:%d (pid=%lu)", g_port,
               (unsigned long)GetCurrentProcessId());
    for (;;) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        handleConn(c);
    }
}

} // namespace wxhook
