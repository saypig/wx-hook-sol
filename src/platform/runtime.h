// runtime.h -- logging, module base, safe reads, WeixinString
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace wxhook {
namespace platform {

void logf(const char* fmt, ...);
void setLogPort(int port);

uintptr_t base();

template <typename T>
T fn(uint64_t rva) {
    return (T)(base() + rva);
}

bool rpm(uintptr_t addr, void* out, size_t n);
uint64_t rpmPtr(uintptr_t addr);
uint32_t rpmU32(uintptr_t addr);
bool rpmCStrEq(uintptr_t p, const char* s);
std::string rpmCStr(uintptr_t p, size_t maxLen = 400);

struct Sections {
    uintptr_t textBase, textEnd, rdataBase, rdataEnd, dataBase, dataEnd;
};
const Sections& sections();

#pragma pack(push, 1)
struct WeixinString {
    union {
        char inline_buf[16];
        char* heap_buf;
    };
    uint64_t length;
    uint64_t cap;
};
#pragma pack(pop)
static_assert(sizeof(WeixinString) == 0x20, "WeixinString must be 0x20");

void setWx(void* dst, const std::string& s);

uint64_t imageSize();
bool versionMatches();

} // namespace platform
} // namespace wxhook
