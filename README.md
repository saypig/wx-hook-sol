# wx-hook-sol

只做四件事：发文本、发图片、发视频、拍一拍。

## 免责声明

> **本项目仅供个人学习、技术研究与交流使用，严禁用于商业用途或任何违法场景。**

- 本项目通过 hook 微信客户端实现消息发送，**可能违反腾讯的服务协议**。由此导致的账号封禁、数据丢失、聊天记录损坏等后果，由使用者自行承担。
- 严禁将本项目用于群发营销、骚扰他人、诈骗、传播违法违规或侵权内容等行为。
- 严禁用于侵犯他人隐私、窃取账号或任何未经授权的用途。
- 本项目按「现状」提供，作者不对因使用本项目造成的任何直接或间接损失负责，也不提供任何形式的担保。
- 本项目与腾讯公司无关，未获其授权或认可。
- 下载或使用本项目即表示你已理解并接受上述条款；如不同意，请立即停止使用并删除本项目。

## 目标版本

- WeChat **4.1.15.13**（`Weixin.dll` 的 `SizeOfImage = 0xC0AE000`）。
- 版本不匹配时 DLL 只注册 `/status` 和 `/ping`，不装发送 hook。
- 微信升级后 RVA 会失效，需要重新迁移 `offsets.h`。

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

注入器会把 `wxhook.dll` 复制到 `%TEMP%`，在副本旁写 `.home` 旁挂文件指回 `dist`，再远程 `LoadLibraryW`。日志在 `dist\wxhook_out.txt`。

## 接口

固定监听 `127.0.0.1:30002`。

| 方法 | 路径 | 请求体 |
|---|---|---|
| POST | `/SendTextMsg` | `{"to_wxid":"...", "content":"..."}` |
| POST | `/SendImgMsg` | `{"to_wxid":"...", "path":"..."}` |
| POST | `/SendVideoMsg` | `{"to_wxid":"...", "path":"..."}` |
| POST | `/SendPatMsg` | `{"to_wxid":"会话", "member_wxid":"被拍人(可省略)"}` |
| GET/POST | `/status` | 无 |
| GET | `/ping` | 无 |

返回：成功 `{"ret":0,...}`；失败 `ret=-1`。图片/视频同一时刻一张，最小间隔 3 秒。

## 目录

```
src/app/         DLL 引导、路由注册与请求分发
src/platform/    RVA 偏移、父目录解析、日志/安全读/WeixinString
src/transport/   本地 HTTP 服务与 JSON 路由
src/wechat/      逻辑线程队列、TLS getter hook、图片/视频发送、VEH 救援、当前 wxid
src/features/    文本/图片/视频、拍一拍、/status 与登录闸
src/injector/    最小注入器
third_party/     minhook + json.hpp
```

## 协议

本项目以 [MIT License](LICENSE) 开源，Copyright (c) 2026 xiaotao。

`third_party/` 下的 MinHook（BSD 2-Clause）与 nlohmann/json（MIT）遵循各自原始协议，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 技术交流

- QQ：**348385675**
- 问题反馈建议优先提 Issue，方便其他人和后来者检索。
