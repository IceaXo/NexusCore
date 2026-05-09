#!/usr/bin/env python3
# ===================================================================
# bot_players.py — 自动填充房间的机器人玩家
#
# 用法:
#   python3 bot_players.py [--count N] [--port 8080] [--room 1]
#
# 每个 bot 流程:
#   TCP连接 → SET_NAME → JOIN_ROOM → READY → 自动游戏
#
# 协议: 4字节大端包头 + JSON body (与服务端/客户端一致)
# ===================================================================

import socket
import struct
import json
import time
import random
import threading
import argparse
import sys

# ===================================================================
# 预设名字和头像颜色
# ===================================================================
BOT_NAMES = ["SKULL", "PANTHER", "FOX", "QUEEN", "JOKER"]

# ===================================================================
# 协议工具函数
# ===================================================================

def pack(msg: dict) -> bytes:
    """将 dict 打包为 4字节大端长度 + UTF-8 JSON"""
    body = json.dumps(msg, ensure_ascii=False).encode("utf-8")
    return struct.pack(">I", len(body)) + body


def unpack(data: bytes) -> tuple:
    """
    从字节流中尝试提取一条完整消息。
    返回 (msg_dict, consumed_bytes) 或 (None, 0)
    """
    if len(data) < 4:
        return None, 0
    body_len = struct.unpack(">I", data[:4])[0]
    if body_len > 64 * 1024:
        raise ValueError(f"Packet too large: {body_len}")
    if len(data) < 4 + body_len:
        return None, 0
    body = data[4:4 + body_len].decode("utf-8")
    return json.loads(body), 4 + body_len


# ===================================================================
# Bot 类 —— 每个实例是一个独立 TCP 连接
# ===================================================================

class BotPlayer:
    def __init__(self, bot_id: int, host: str = "127.0.0.1", port: int = 8080, room_id: int = 1):
        self.bot_id = bot_id
        self.host = host
        self.port = port
        self.room_id = room_id
        self.sock: socket.socket | None = None
        self.buffer = b""
        self.running = False
        self.name = f"BOT_{bot_id}"
        self.avatar = bot_id % 5
        self.my_seat = -1
        self.hint_options = []

        # 使用预设名字（如果 id 在范围内）
        if bot_id < len(BOT_NAMES):
            self.name = f"AI_{BOT_NAMES[bot_id]}"

    # ---- 连接与登录流程 ----

    def connect(self) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(5)
            self.sock.connect((self.host, self.port))
            self.sock.settimeout(None)
            self.running = True
            print(f"[{self.name}] 已连接 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"[{self.name}] 连接失败: {e}")
            return False

    def login(self):
        """发送 SET_NAME → JOIN_ROOM → READY"""
        self.send({"action": "SET_NAME", "name": self.name, "avatar": self.avatar})
        time.sleep(0.3)
        self.send({"action": "JOIN_ROOM", "room_id": self.room_id})
        time.sleep(0.3)
        self.send({"action": "READY"})

    # ---- 消息收发 ----

    def send(self, msg: dict):
        if self.sock:
            try:
                self.sock.sendall(pack(msg))
            except Exception as e:
                print(f"[{self.name}] 发送失败: {e}")

    def recv_loop(self):
        """阻塞收包循环，收到消息后调用 handle()"""
        while self.running and self.sock:
            try:
                data = self.sock.recv(4096)
                if not data:
                    print(f"[{self.name}] 服务器断开连接")
                    break
                self.buffer += data
                while True:
                    msg, consumed = unpack(self.buffer)
                    if msg is None:
                        break
                    self.buffer = self.buffer[consumed:]
                    self.handle(msg)
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"[{self.name}] 收包异常: {e}")
                break
        self.running = False

    # ---- 游戏逻辑 ----

    def handle(self, msg: dict):
        t = msg.get("type", "")
        state = msg.get("state", "")
        action = msg.get("action", "")

        # 错误处理
        if t == "error":
            print(f"[{self.name}] 服务端错误: {msg.get('message', '')}")
            return

        # 改名确认：收到 set_name_ok，登录流程已在 login() 里处理
        if t == "set_name_ok":
            print(f"[{self.name}] 改名成功")
            return

        # 房间列表：不需要处理
        if t == "room_list":
            return

        # HINT 响应
        if t == "hint":
            self.hint_options = msg.get("options", [])
            self._handle_hint_response()
            return

        # 按游戏状态分发
        self.my_seat = msg.get("my_seat", self.my_seat)

        if state == "WAITING":
            self._handle_waiting(msg)
        elif state == "BIDDING":
            self._handle_bidding(msg)
        elif state == "BOTTOM_PICK":
            self._handle_bottom_pick(msg)
        elif state == "PLAYING":
            self._handle_playing(msg)
        elif state == "END":
            self._handle_end(msg)

    def _handle_waiting(self, msg: dict):
        pass  # 已 READY，等待开始

    def _handle_bidding(self, msg: dict):
        cb = msg.get("current_bidder", -1)
        if cb == self.my_seat:
            # 50% 概率叫地主
            if random.random() < 0.5:
                print(f"[{self.name}] CALL")
                self.send({"action": "CALL"})
            else:
                print(f"[{self.name}] PASS (bidding)")
                self.send({"action": "PASS"})

    def _handle_bottom_pick(self, msg: dict):
        if msg.get("is_picking") and msg.get("bottom_pick_landlord") == self.my_seat:
            indices = sorted(random.sample(range(4), 2))
            print(f"[{self.name}] PICK_BOTTOM indices={indices}")
            self.send({"action": "PICK_BOTTOM", "indices": indices})

    def _handle_playing(self, msg: dict):
        ct = msg.get("current_turn", -1)
        if ct != self.my_seat:
            return
        # 请求 HINT 获取可出牌组合
        print(f"[{self.name}] 请求 HINT")
        self.send({"action": "HINT"})

    def _handle_hint_response(self):
        if self.hint_options:
            # 选第一个 hint 组合出牌
            choice = self.hint_options[0]
            print(f"[{self.name}] PLAY {len(choice)} cards")
            self.send({"action": "PLAY", "cards": choice})
        else:
            # 无牌可出，过
            print(f"[{self.name}] PASS (playing)")
            self.send({"action": "PASS"})

    def _handle_end(self, msg: dict):
        print(f"[{self.name}] 本局结束，发送 CONTINUE")
        time.sleep(1.0)
        self.send({"action": "CONTINUE"})

    def stop(self):
        self.running = False
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None


# ===================================================================
# 启动 bot 群
# ===================================================================

def main():
    parser = argparse.ArgumentParser(description="NexusCore Bot Players")
    parser.add_argument("--count", type=int, default=5, help="机器人数量 (默认 5)")
    parser.add_argument("--port", type=int, default=8080, help="服务器端口 (默认 8080)")
    parser.add_argument("--room", type=int, default=1, help="房间号 (默认 1)")
    parser.add_argument("--host", default="127.0.0.1", help="服务器地址 (默认 127.0.0.1)")
    args = parser.parse_args()

    bots = []
    threads = []

    print(f"启动 {args.count} 个 bot → {args.host}:{args.port} room={args.room}")
    print("-" * 40)

    for i in range(args.count):
        bot = BotPlayer(i, host=args.host, port=args.port, room_id=args.room)
        if not bot.connect():
            print(f"Bot {i} 连接失败，退出")
            sys.exit(1)
        bot.login()
        bots.append(bot)
        t = threading.Thread(target=bot.recv_loop, daemon=True)
        t.start()
        threads.append(t)
        time.sleep(0.2)

    print("-" * 40)
    print(f"{len(bots)} 个 bot 全部就绪，Ctrl+C 停止")

    try:
        while any(t.is_alive() for t in threads):
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n停止所有 bot...")
        for b in bots:
            b.stop()
        for t in threads:
            t.join(timeout=2)
        print("已退出")


if __name__ == "__main__":
    main()
