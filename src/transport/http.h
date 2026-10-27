// http.h -- tiny JSON router used by the local HTTP transport
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

namespace wxhook {
namespace http {

using nlohmann::json;

struct Req {
    std::string method;
    std::string path;
    std::string body;
    json j() const;
};

using Handler = std::function<std::string(const Req&)>;

constexpr uint64_t fnv1a(const char* s, size_t n) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= (unsigned char)s[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

template <size_t N>
constexpr uint64_t phash(const char (&s)[N]) {
    return fnv1a(s, N - 1);
}

inline uint64_t phash_cstr(const char* s) {
    return s ? fnv1a(s, strlen(s)) : 0;
}

#define ROUTE(s) ([] { constexpr uint64_t v = ::wxhook::http::phash(s); return v; }())

enum : uint8_t { M_GET = 1, M_POST = 2 };

class Router {
public:
    void add(uint8_t method, uint64_t pathHash, Handler h);
    void post(uint64_t pathHash, Handler h) { add(M_POST, pathHash, std::move(h)); }
    void get(uint64_t pathHash, Handler h) { add(M_GET, pathHash, std::move(h)); }
    std::string dispatch(const Req& r) const;

private:
    struct Route {
        uint8_t m;
        uint64_t ph;
        Handler h;
    };
    std::vector<Route> routes_;
};

std::string ok(const json& data = json::object());
std::string err(const std::string& msg);
std::string raw(const json& j);
std::string envelope(const std::string& body);

} // namespace http
} // namespace wxhook
