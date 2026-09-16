# NexusCore 项目导读

以五人牌局为载体的联机游戏原型：Windows 客户端负责交互，Linux C++ 服务端持有房间和牌局状态，Python Agent 为托管玩家提出行动。

## 玩家与服务端流程

玩家连接自己的服务器，设置身份并进入房间，随后进行叫牌和出牌。客户端通过四字节长度头和 JSON 传输消息；服务端完成规则校验、更新牌局并向有关玩家广播。断线恢复使用重连身份和托管交接机制。

模型只能给出候选行动，最终是否合法仍由 C++ 规则层决定。网络收发缓冲、消息边界、牌局状态和模型返回是四种不同的问题，分别由对应模块处理。

## 核心代码

- [EpollServer](../server/network)：非阻塞网络、消息收发与连接处理。
- [CardRule.cpp](../server/game/CardRule.cpp)：牌型识别和比较。
- [Room.cpp](../server/game/Room.cpp)：牌局流程、动作检查、广播和恢复。
- [agent_brain.py](../server/agent/agent_brain.py)：模型输入、结果解析和失败兜底。
- [ClientOptions.h](../client/ClientOptions.h)：本地启动默认值和参数校验。
- [WebViewHost.cpp](../client/ui/WebViewHost.cpp)：网页 UI 与 TCP 客户端桥接。

## 本地启动路线

从仓库根目录编译服务端后，在 server 目录启动，使其读到现有 config.json：

```bash
cmake -S server -B server/build
cmake --build server/build
cd server
./build/nexus_server --http-root ../client/html --log ./server.log
```

默认游戏端口 8080、HTTP 页面端口 7778、Agent IPC 端口 8081。Windows 客户端默认连接 127.0.0.1:8080；远端自建环境通过 --server、--port、--url 显式指定。若没有模型配置，不能将托管能力视为已可用。

```powershell
.\client\build\Release\nexus_client.exe --server 127.0.0.1 --port 8080 --url http://127.0.0.1:7778/p5_ui.html
```

客户端构建需要 VS2022 和 WebView2 SDK，详见首页。

## 验证记录与边界

2026-09-14 使用 GCC 15.2 实际运行 client/tests/ClientOptionsTests.cpp，以及现有 test_comprehensive.cpp 的 175 项牌型测试，全部通过。前者覆盖默认地址、数字端口范围、缺失参数和未知参数。

这不包含 epoll 服务运行、Windows WebView2 联调、重连异常、真实模型或并发压测。旧文档中的万级连接等数字继续保留为历史设计目标，不当作当前测量结果。

## 后续应用：OneStepCards

五人斗地主的规则、阶段流转和测试向量后来被迁移到 Godot 的多玩法应用 OneStepCards。它使用 Steam 大厅与房主权威状态，复用牌桌、设置和更新器。迁移对象与原 C++ 网络原型各有分工，详见 [NexusCore → OneStepCards](ONESTEPCARDS_EVOLUTION.md)。

[返回首页](../README.md)
