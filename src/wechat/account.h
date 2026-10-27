// account.h -- resolve the logged-in wxid from WeChat's open handles
#pragma once
#include <string>

namespace wxhook {
namespace account {

void init();
std::string selfWxid();

} // namespace account
} // namespace wxhook
