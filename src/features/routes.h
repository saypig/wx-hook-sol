// routes.h -- feature registration and login gate
#pragma once
#include <string>

#include "transport/http.h"

namespace wxhook {
namespace features {

void register_send(http::Router& r);
void register_pat(http::Router& r);
void register_status(http::Router& r);

bool loginGateBlocks(const std::string& path);
const char* sendDeniedIfLoggedOut();

} // namespace features
} // namespace wxhook
