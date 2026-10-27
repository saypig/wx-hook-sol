// offsets.h -- minimal RVAs for WeChat 4.1.15.13 (text / image / video / pat)
#pragma once
#include <cstdint>

namespace wxhook {
namespace wxoff {

inline constexpr const char* WX_VER = "4.1.15.13";
inline constexpr uint64_t MODULE_SIZE = 0xC0AE000;

// text
inline constexpr uint64_t CREATE_MSG_OBJ = 0x6F4C70;
inline constexpr uint64_t GET_ROOT = 0x47220;
inline constexpr uint64_t GET_SUB = 0x36C740;
inline constexpr uint64_t GET_SVC = 0x6F4D90;
inline constexpr uint64_t SEND_ONE = 0x178F100;
inline constexpr uint64_t WX_ALLOC = 0x74AB89C;
inline constexpr uint64_t MSG_RECEIVER = 0xB0;
inline constexpr uint64_t MSG_CONTENT = 0x758;
inline constexpr uint64_t MSG_TYPE_9C = 0x9C;
inline constexpr uint64_t MSG_TYPE_118 = 0x118;
inline constexpr uint64_t MSG_SUBTYPE_11C = 0x11C;
inline constexpr int32_t MSGTYPE_TEXT = 1;
inline constexpr int32_t MSGTYPE_APPMSG = 49;

// image / video
inline constexpr uint64_t MAKE_IMG_CONTENT = 0x1D670F0;
inline constexpr uint64_t MAKE_VIDEO_CONTENT = 0x2F267D0;
inline constexpr uint64_t TLS_GETTER = 0x48DC30;
inline constexpr uint64_t TLS_INDEX_GLB = 0xB9141A0;
inline constexpr uint64_t TLS_CTX_FIELD = 0x1F0;
inline constexpr uint64_t IMG_BAD_DTOR = 0xE9E920;
inline constexpr uint64_t IMG_BAD_DTOR_TAIL = 0xE9E9E1;
inline constexpr uint64_t IMG_BAD_MEMCPY = 0x7544050;
inline constexpr uint64_t IMG_BAD_MEMCPY_RET = 0x754407E;

// pat
inline constexpr uint64_t SEND_PAT_CORE = 0x398B760;
inline constexpr uint64_t PAT_SVC_GET = 0x17AE840;

// sqlite3_vfs / sqlite3 / Btree layout
inline constexpr int VFS_SZOSFILE = 0x04;
inline constexpr int VFS_MXPATHNAME = 0x08;
inline constexpr int VFS_ZNAME = 0x18;
inline constexpr int DB_ADB = 0x20;
inline constexpr int DB_NDB = 0x28;
inline constexpr int DBREC_PBT = 0x08;
inline constexpr int BTREE_PBT = 0x08;
inline constexpr int BTSH_PAGER = 0x00;

} // namespace wxoff
} // namespace wxhook
