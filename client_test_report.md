# client/ 需求实现测试报告

> 对照文档：技术文档.md / 需求文档.md / 设计文档.md / 规则文档.md / README.md
> 测试日期：2026-05-07

---

## 一、完全实现 ✅

| # | 需求项 | 来源 | 实现位置 |
|---|--------|------|----------|
| 1 | Win32 原生窗口 + WebView2 嵌入 | 技术文档 §三.1 | `WebViewHost.cpp:43-49` |
| 2 | CMake 构建系统 (VS2022 + WebView2 SDK) | 技术文档 §五 | `client/CMakeLists.txt` |
| 3 | 4 字节大端包头 Length-Prefixed 协议 | 技术文档 §二.1 | `TcpClient.cpp:67-69` + `ReadExact:91-102` |
| 4 | C++ → JS 桥接 (PostWebMessageAsJson) | 技术文档 §三.2 | `WebViewHost.cpp:260` |
| 5 | JS → C++ 桥接 (window.chrome.webview.postMessage) | 技术文档 §三.2 | `p5_ui.html:598-603` + `WebViewHost.cpp:209-221` |
| 6 | TCP 收包 → JS 推送管道 (main 串联) | 技术文档 §一 | `main_client.cpp:23-31` |
| 7 | CSS clip-path: polygon() 多边形切割 | 设计文档 §I | `p5_ui.html:161, 285` |
| 8 | 全局 border-radius: 0 | 设计文档 §I | 全文件无任何 border-radius |
| 9 | 硬阴影 (blur=0) | 设计文档 §I | `box-shadow: Xpx Xpx 0px 0px` 多处 |
| 10 | 扑克牌三态 (idle / hover / selected) | 设计文档 §III | `p5_ui.html:297-311, 641-648, 696-712` |
| 11 | 扇形手牌排列 (radius=1600, angleStep=3.8°) | 设计文档 §III | `p5_ui.html:609-638` |
| 12 | PASS / STRIKE!! / HINT 三按钮 | 设计文档 §IV | `p5_ui.html:537-550` |
| 13 | HINT 按钮红色硬阴影 | 设计文档 §IV | `p5_ui.html:447` box-shadow red |
| 14 | 半调网点背景 (radial-gradient) | 设计文档 §II.1 | `p5_ui.html:65-71` |
| 15 | 水印文字 (VOID GAZER) | 设计文档 §II.1 | `p5_ui.html:74-84` |
| 16 | 倍数计分板 (Session Multiplier) | 设计文档 §II.2 | `p5_ui.html:89-107` |
| 17 | 非对称梯形玩家头像框 | 设计文档 §II.2 | `p5_ui.html:156-161` |
| 18 | 大王/小王特殊显示 (星标 + 金色闪光) | 设计文档 §III | `p5_ui.html:367-409` |
| 19 | 结果覆盖层 (VICTORY/DEFEAT) | 设计文档 | `p5_ui.html:488-506` |
| 20 | 扫描线效果 | 设计文档 | `p5_ui.html:476-483` |

---

## 二、偏离设计规范 ⚠️

| # | 问题 | 规范要求 | 当前实现 | 严重程度 |
|---|------|----------|----------|:---:|
| 1 | 主红色值错误 | `#E60012` (Blood Red) | `#dc2626` (Tailwind red-600) | 中 |
| 2 | 深渊黑值错误 | `#111111` (Void Black) | `#000000` (纯黑) | 低 |
| 3 | 字体族不符 | `Anton` 或 `Oswald` | `Impact, 'Arial Black'` (系统回退) | 中 |
| 4 | 字间距反向 | `letter-spacing: -1px ~ -2px` (压迫感) | 多处正向展开 `0.04em~0.5em` | 中 |
| 5 | 缺少 font-weight: 900 | 全局 `font-weight: 900` | 未强制指定，依赖字体默认 | 低 |
| 6 | 选中态光效节奏不符 | "不规则快速频闪" pulse | 匀速 glowPulse 2s ease-in-out | 低 |
| 7 | STRIKE 悬停无颤动 | "高频随机位移" | 仅 `translateY(-6px)` | 中 |
| 8 | 出牌瞬间动画缺失 | 屏幕闪白 + 牌向中央集合 + 镜头倾斜 | 直接清空选中态，无过渡 | 高 |
| 9 | Socket 模式不符 | 技术文档要求非阻塞或 IOCP | `recv()` 阻塞 + 独立线程 | 低 |

---

## 三、未实现 ❌

| # | 需求项 | 来源 | 说明 |
|---|--------|------|------|
| 1 | 主菜单/大厅界面 | 需求文档 §四 / README TODO#1 | 无大厅：房间列表、创建/加入房间、昵称设置。HTML 直接进入战斗 |
| 2 | 叫地主阶段弹出层 | 设计文档 §IV | 缺 TAKEOVER / DECLINE 按钮及 scale 3→1 砸入动画 |
| 3 | 菜单项撕裂纸条效果 | 设计文档 §II.1 | 缺 clip-path 毛边切割的主菜单项 |
| 4 | Menu Hover 颜色反转 | 设计文档 §II.1 | 缺 translateX(20px) + 红白反转 |
| 5 | 中心结算区 (Last Action Area) | 设计文档 §II.2 | 缺半透明黑色磨砂遮罩 + 牌型 1.5s 渐隐放大消散 |
| 6 | 心跳机制 (Ping/Pong) | 需求文档 §三 / README TODO#2 | TcpClient 不发送心跳，5s 断线接管仅依赖服务端 |
| 7 | 命令行参数解析 | README TODO#3 | IP/端口硬编码 `127.0.0.1:8080` |
| 8 | 断线重连 + 指数退避 | README TODO#4 | TcpClient 断开后无自动重连 |
| 9 | 响应式布局 | README TODO#7 | body 硬编码 1600×900px，不可缩放 |
| 10 | 客户端 C++ 单元测试 | README TODO#6 | TcpClient / WebViewHost 无测试覆盖 |

---

## 四、总结

**核心数据管道完整**：TCP ↔ WebView2 ↔ HTML/JS 双向通信全链路可运行，战斗 HUD（手牌渲染、玩家头像、计分板、动作按钮）骨架就位，可与 `test_mock_server.py` 配合进行端到端出牌测试。

**优先修复建议**：

1. **Bidding UI** — 叫地主是游戏流程必经阶段，前端完全缺失
2. **出牌动画** — 设计文档要求的闪白、牌集合、镜头倾斜等核心动效未实现
3. **主菜单/大厅** — 当前无房间选择与创建流程
4. **视觉色值/字体对齐** — 主红色、字体族、字间距与设计文档不一致
5. **心跳 + 重连** — 断线容灾的客户端侧配合逻辑缺失
