// http.cpp -- Req/Router/response helpers
#include "transport/http.h"

namespace wxhook {
namespace http {

json Req::j() const {
    try {
        return body.empty() ? json::object() : json::parse(body);
    } catch (...) {
        return json::object();
    }
}

void Router::add(uint8_t method, uint64_t pathHash, Handler h) {
    routes_.push_back({method, pathHash, std::move(h)});
}

std::string Router::dispatch(const Req& r) const {
    const uint64_t ph = fnv1a(r.path.data(), r.path.size());
    const uint8_t m = (r.method == "POST") ? M_POST : (r.method == "GET" ? M_GET : 0);
    for (const auto& rt : routes_)
        if (rt.m == m && rt.ph == ph) return rt.h(r);
    for (const auto& rt : routes_)
        if (rt.ph == ph) return rt.h(r);
    return err("unknown path: " + r.path);
}

static std::string dumpSafe(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string ok(const json& data) {
    json o;
    o["ret"] = 0;
    o["data"] = data;
    return dumpSafe(o);
}

std::string err(const std::string& msg) {
    json o;
    o["ret"] = -1;
    o["msg"] = msg;
    return dumpSafe(o);
}

std::string raw(const json& j) { return dumpSafe(j); }

std::string envelope(const std::string& body) {
    json o;
    try {
        o = json::parse(body);
    } catch (...) {
        return body;
    }
    if (o.is_array()) {
        json out;
        out["ret"] = 0;
        out["data"] = o;
        return dumpSafe(out);
    }
    if (!o.is_object()) return body;

    json out;
    out["ret"] = 0;
    if (o.contains("ret") && o["ret"].is_number()) out["ret"] = o["ret"];
    o.erase("ret");

    std::string msg;
    if (o.contains("msg") && o["msg"].is_string()) {
        msg = o["msg"].get<std::string>();
        o.erase("msg");
    }
    if (msg.empty() && o.contains("retmsg") && o["retmsg"].is_string()) {
        msg = o["retmsg"].get<std::string>();
    }
    o.erase("retmsg");

    if (o.contains("data") && o["data"].is_object()) {
        json inner = o["data"];
        o.erase("data");
        o.update(inner);
    }

    if (!msg.empty()) out["msg"] = msg;
    if (out["ret"].get<long long>() == 0 || !o.empty()) out["data"] = std::move(o);
    return dumpSafe(out);
}

} // namespace http
} // namespace wxhook
