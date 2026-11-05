# wx-hook-sol

微信本地 HTTP 工具：发文本、发图片、发视频、拍一拍，以及获取好友列表、群列表和群成员列表。

## 免责声明

> **本项目仅供个人学习、技术研究与交流使用，严禁用于商业用途或任何违法场景。**

- 本项目通过 hook 微信客户端实现消息发送，**可能违反腾讯的服务协议**。由此导致的账号封禁、数据丢失、聊天记录损坏等后果，由使用者自行承担。
- 严禁将本项目用于群发营销、骚扰他人、诈骗、传播违法违规或侵权内容等行为。
- 严禁用于侵犯他人隐私、窃取账号或任何未经授权的用途。
- 本项目按「现状」提供，作者不对因使用本项目造成的任何直接或间接损失负责，也不提供任何形式的担保。
- 本项目与腾讯公司无关，未获其授权或认可。
- 下载或使用本项目即表示你已理解并接受上述条款；如不同意，请立即停止使用并删除本项目。

## 支持版本

- WeChat **4.1.15.13**。其他版本不保证可用。

## 构建

需要 MinGW-w64（x86_64，win32 线程模型）：

```bash
cd wx-hook-sol
mingw32-make -j4
```

产物：

```
dist/wxhook.dll
dist/wxhook_inject.exe
```

## 注入

1. 先重启微信，确保进程里没有旧版注入模块或其它注入器（不要叠加注入）。
2. 以与微信相同或更高权限运行：

```bash
dist\wxhook_inject.exe
```

注入成功后，接口监听 `127.0.0.1:30002`，日志在 `dist\wxhook_out.txt`。

## 接口

固定监听 `127.0.0.1:30002`。

| 方法 | 路径 | 请求体 |
|---|---|---|
| POST | `/SendTextMsg` | `{"to_wxid":"...", "content":"..."}` |
| POST | `/SendImgMsg` | `{"to_wxid":"...", "path":"..."}` |
| POST | `/SendVideoMsg` | `{"to_wxid":"...", "path":"..."}` |
| POST | `/SendPatMsg` | `{"to_wxid":"会话", "member_wxid":"被拍人(可省略)"}` |
| POST | `/GetFriends` | `{"keyword":"可选"}` |
| POST | `/GetGroups` | `{"keyword":"可选"}` |
| POST | `/GetRoomMembers` | `{"room_wxid":"...@chatroom"}` |
| GET/POST | `/status` | 无 |
| GET | `/ping` | 无 |

返回：成功 `{"ret":0,...}`；失败 `ret=-1`。图片/视频同一时刻一张，最小间隔 3 秒。

列表接口返回 `{"ret":0,"data":{"count":N,"items":[...]}}`。`/GetFriends` 和 `/GetGroups` 支持 `keyword` 模糊搜索（匹配备注、昵称、微信号和 wxid）；`/GetRoomMembers` 返回 `username`、`nickname`、`remark`、头像和 `local_type`（`local_type` 可判断群成员是否为好友）。

Postman 调试：在 Postman 里 Import [docs/wx-hook-sol.postman_collection.json](docs/wx-hook-sol.postman_collection.json)，集合内已带 `baseUrl`、`to_wxid`、`img_path`、`video_path` 等变量，改完即可逐个发送。

## 协议

本项目以 [MIT License](LICENSE) 开源，Copyright (c) 2026 xiaotao。

`third_party/` 下的 MinHook（BSD 2-Clause）与 nlohmann/json（MIT）遵循各自原始协议，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 技术交流

- QQ：**1442343981**
- 问题反馈建议优先提 Issue，方便其他人和后来者检索。
