// media.h -- image/video send path
#pragma once

namespace wxhook {

enum { BSM_IMAGE = 0, BSM_VIDEO = 1 };

bool imgsend_install();
bool imgSend(const wchar_t* wxid, const wchar_t* path, unsigned timeoutMs);
bool mediaSend(const wchar_t* wxid, const wchar_t* path, unsigned timeoutMs, int kind);

} // namespace wxhook
