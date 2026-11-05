// account.h -- resolve the logged-in wxid from WeChat's open handles
#pragma once
#include <cstdint>
#include <string>

namespace wxhook {
namespace account {

void init();
std::string selfWxid();

// sqlite3* of an already-open database file (e.g. "contact.db"), 0 if not open.
uintptr_t handleFor(const std::string& basename);

} // namespace account
} // namespace wxhook
