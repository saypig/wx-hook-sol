// server.h -- local HTTP transport and the app dispatch seam
#pragma once

namespace wxhook {

#define WXHOOK_PORT 30002

typedef void (*wxhook_dispatch_t)(const char* method, const char* path,
                                  const char* body, int bodyLen,
                                  void (*emit)(void* u, const char* data, int len),
                                  void* u);

void server_serve(int port);

} // namespace wxhook

extern "C" int wxhook_init(void);
extern "C" void wxhook_dispatch(const char* method, const char* path,
                                const char* body, int bodyLen,
                                void (*emit)(void* u, const char* data, int len),
                                void* u);
