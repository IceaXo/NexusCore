# NexusCore · C++ 联机牌局与 LLM 托管原型

以五人牌局为业务载体，实践 Linux epoll 网络、消息分帧、服务端规则校验、断线恢复，以及 Python 大模型托管。客户端由 Windows C++ 宿主与 WebView2 UI 组成。

个人实践覆盖网络层与游戏逻辑分离、房间状态流转、C++/Python 消息桥接和客户端集成。模型负责提出行动，C++ 规则层仍需验证该行动；当前定位为原型，不承诺生产容量或服务等级。

## 现有功能与入口

| 部分 | 已有实现 | 入口 |
| --- | --- | --- |
| 网络 | 非阻塞 epoll、长度前缀消息、收发缓冲、静态文件服务 | [server/network](server/network) |
| 规则与牌局 | 牌型比较、叫牌/出牌状态、序列化与广播 | [CardRule.cpp](server/game/CardRule.cpp)、[Room.cpp](server/game/Room.cpp) |
| 房间与恢复 | 房间路由、玩家状态、重连 Token 和托管交接 | [RoomManager.cpp](server/game/RoomManager.cpp)、[Room.cpp](server/game/Room.cpp) |
| 模型托管 | Prompt、模型请求、结果解析和 fallback | [agent_brain.py](server/agent/agent_brain.py)、[server/ipc](server/ipc) |
| Windows 客户端 | TCP 与 WebView2 桥接、启动参数 | [main_client.cpp](client/main_client.cpp)、[client/network](client/network)、[client/ui](client/ui) |
| 交互 | 大厅、牌桌、消息协议与音频模块 | [client/html](client/html) |

```mermaid
flowchart LR
    U[WebView2 UI] <--> W[Windows TCP 客户端]
    W <-->|4字节长度头与 JSON| N[Linux epoll]
    N <--> R[房间状态与规则校验]
    R <--> I[C++ / Python IPC]
    I <--> A[LLM 决策与 fallback]
```

旧 README 曾将 Agent、出牌流程、广播和启动参数列为待办；当前源码已有这些实现，本页已按真实入口更新。连接恢复机制存在不代表所有异常网络场景均通过验证。

## 构建与配置

服务端使用 Linux/WSL、C++14 和 [server/CMakeLists.txt](server/CMakeLists.txt)；Windows 客户端使用 VS2022、WebView2 Runtime/SDK 与 [client/CMakeLists.txt](client/CMakeLists.txt)。从仓库根目录构建对应部分：

```bash
cmake -S server -B server/build
cmake --build server/build
```

```powershell
cmake -S client -B client/build -G "Visual Studio 17 2022" -A x64
cmake --build client/build --config Release
```

WebView2 SDK 的目录和 NuGet 安装示例见客户端 CMake 注释。启动前按 [server/main.cpp](server/main.cpp) 的 config.json 读取规则配置端口、房间数和 HTTP 目录。Python Agent 的 IPC 监听默认为本机 8081，应与服务端设置一致；模型凭据由运行者在本地提供。

客户端支持 `--server`、`--port`、`--url`、`--file`。现有默认值仍指向历史公网部署，演示时应显式指定自己的服务器与 UI 地址。不要将历史地址可达性当成项目已部署可用的证明。

## 验证与后续

- [规则测试](server/game)、[压力脚本](server/agent/stress_test.py) 和 [客户端报告](client_test_report.md) 已保留，可查看断言、场景和历史记录。
- 2026-05-06 文档记录过 446 项规则测试通过；2026-09-14 的本次整理未重跑测试、服务或模型。
- 原并发 10,000、2,000 房间、AI 3 秒内等数字是设计目标，未作为本轮实测成绩。
- 后续需要按独立环境复核网络异常、参数校验、真实容量与客户端交互；默认公网地址也应转为更明确的本地配置。

本轮更新文档与导航，未修改运行代码。当前没有 LICENSE 文件，原 MIT 徽章已移除；没有因文案整理新增授权。

[历史开发记录](https://github.com/IceaXo/NexusCore/blob/c4812092c0b917fbd63eb7ce6a3cc19bc78fefcb/README.md) · [更多项目](https://github.com/IceaXo)
