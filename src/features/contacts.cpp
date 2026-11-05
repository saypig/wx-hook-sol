// contacts.cpp -- read-only contact lists from the client's open contact.db
//
//   POST /GetFriends      {"keyword":"可选"}                好友列表
//   POST /GetGroups       {"keyword":"可选"}                群列表
//   POST /GetRoomMembers  {"room_wxid":"...@chatroom"}      群成员列表
//
// All three run in-process against the SQLite handle WeChat already holds, so
// they need no key, no network and no logic-thread hop.
#include <string>

#include "features/routes.h"
#include "wechat/account.h"
#include "wechat/db.h"

using namespace wxhook;
using http::json;

namespace {

// WeChat's built-in service accounts are not real friends.
const char* kSysAccounts =
    "('medianote','floatbottle','mphelper','filehelper','weixin','fmessage',"
    "'weixinguanhaozhushou','qqmail','weibo','tmessage','qqsafe','qqsync',"
    "'shakesong','weixinreminder','notification_messages','brandsessionholder')";

const char* kContactFields =
    "c.username,c.local_type,COALESCE(c.alias,'') AS alias,"
    "COALESCE(c.remark,'') AS remark,COALESCE(c.nick_name,'') AS nickname,"
    "c.flag,c.verify_flag,"
    "COALESCE(c.big_head_url,'') AS big_head_url,"
    "COALESCE(c.small_head_url,'') AS small_head_url";

bool safeId(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '@' || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string sqlEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c == '\'') r += "''";
        else r.push_back(c);
    }
    return r;
}

json rowsToJson(const db::Rows& rows) {
    json arr = json::array();
    for (const auto& row : rows) {
        json o = json::object();
        for (const auto& kv : row) o[kv.first] = kv.second;
        arr.push_back(std::move(o));
    }
    return arr;
}

std::string contactList(const http::Req& req, const std::string& cond) {
    std::string where = cond;
    const std::string kw = req.j().value("keyword", std::string());
    if (!kw.empty()) {
        if (kw.size() > 128) return http::err("bad keyword");
        const std::string ek = sqlEscape(kw);
        where += " AND (c.remark LIKE '%" + ek + "%' OR c.nick_name LIKE '%" + ek +
                 "%' OR c.alias LIKE '%" + ek + "%' OR c.username LIKE '%" + ek + "%')";
    }
    db::Rows rows;
    std::string err;
    if (!db::query("contact.db",
                   "SELECT " + std::string(kContactFields) + " FROM contact c WHERE " + where,
                   &rows, &err))
        return http::err(err);
    return http::ok(json{{"count", (long)rows.size()}, {"items", rowsToJson(rows)}});
}

} // namespace

namespace wxhook {
namespace features {

void register_contacts(http::Router& r) {
    r.post(ROUTE("/GetFriends"), [](const http::Req& req) -> std::string {
        std::string cond =
            "c.local_type=1 AND c.delete_flag=0 AND substr(c.username,1,3)<>'gh_' "
            "AND c.username NOT LIKE '%@chatroom' AND c.username NOT IN " +
            std::string(kSysAccounts);
        const std::string self = account::selfWxid();
        if (!self.empty()) cond += " AND c.username<>'" + sqlEscape(self) + "'";
        return contactList(req, cond);
    });

    r.post(ROUTE("/GetGroups"), [](const http::Req& req) -> std::string {
        return contactList(req, "c.delete_flag=0 AND c.username LIKE '%@chatroom'");
    });

    r.post(ROUTE("/GetRoomMembers"), [](const http::Req& req) -> std::string {
        const std::string room = req.j().value("room_wxid", std::string());
        if (!safeId(room)) return http::err("bad room_wxid");

        std::string sql =
            "SELECT n.username AS username,COALESCE(c.nick_name,'') AS nickname,"
            "COALESCE(c.remark,'') AS remark,"
            "COALESCE(c.big_head_url,'') AS big_head_url,"
            "COALESCE(c.small_head_url,'') AS small_head_url,"
            "COALESCE(c.local_type,-1) AS local_type "
            "FROM chatroom_member m JOIN name2id n ON n.rowid=m.member_id "
            "LEFT JOIN contact c ON c.username=n.username "
            "WHERE m.room_id=(SELECT rowid FROM name2id WHERE username='" +
            room + "')";

        db::Rows rows;
        std::string err;
        if (!db::query("contact.db", sql, &rows, &err)) return http::err(err);
        return http::ok(json{{"room_wxid", room},
                             {"count", (long)rows.size()},
                             {"items", rowsToJson(rows)}});
    });
}

} // namespace features
} // namespace wxhook
