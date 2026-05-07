"""4 AI bots that connect to local NexusCore server and auto-play.

Stagger-connect to the same room as the human client (5 total).
- BIDDING: first 2 bidders CALL, rest PASS
- PLAYING: always PASS (lets human drive the action)
- Sends PING heartbeat every 2s
"""

import socket
import struct
import json
import time
import threading
import sys

HOST = "127.0.0.1"
PORT = 8080


def recv_message(sock):
    """Read one 4-byte big-endian length-prefixed JSON message (blocking)."""
    try:
        header = b""
        while len(header) < 4:
            chunk = sock.recv(4 - len(header))
            if not chunk:
                return None
            header += chunk
        body_len = struct.unpack(">I", header)[0]
        if body_len == 0 or body_len > 65536:
            return None
        body = b""
        while len(body) < body_len:
            chunk = sock.recv(body_len - len(body))
            if not chunk:
                return None
            body += chunk
        return json.loads(body.decode("utf-8"))
    except (socket.timeout, OSError):
        return None


def send_message(sock, data):
    """Send a JSON message with 4-byte big-endian length prefix."""
    body = json.dumps(data).encode("utf-8")
    header = struct.pack(">I", len(body))
    try:
        sock.sendall(header + body)
    except OSError:
        pass


def bot_loop(bot_id):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(2.0)  # recv timeout so we can send PING

    try:
        sock.connect((HOST, PORT))
    except OSError as e:
        print(f"[Bot{bot_id}] connect failed: {e}")
        return

    print(f"[Bot{bot_id}] connected (seat={bot_id})")
    last_ping = time.time()
    my_seat = bot_id  # 按连接顺序: bot0→seat0, bot1→seat1, etc. 客户端最后连→seat4

    try:
        while True:
            now = time.time()
            if now - last_ping >= 2.0:
                send_message(sock, {"action": "PING"})
                last_ping = now

            msg = recv_message(sock)
            if msg is None:
                continue  # timeout or empty, loop to send PING

            state = msg.get("state")
            print(f"[Bot{bot_id}] recv state={state} turn={msg.get('current_turn')} bidder={msg.get('current_bidder')}", flush=True)
            if "my_seat" in msg:
                my_seat = msg["my_seat"]

            if state == "BIDDING":
                cb = msg.get("current_bidder", -1)
                if cb == my_seat:
                    fb = msg.get("first_bidder", -1)
                    sb = msg.get("second_bidder", -1)
                    if my_seat in (fb, sb):
                        send_message(sock, {"action": "CALL"})
                        print(f"[Bot{bot_id}] seat={my_seat} CALL")
                    else:
                        send_message(sock, {"action": "PASS"})
                        print(f"[Bot{bot_id}] seat={my_seat} PASS")

            elif state == "PLAYING":
                turn = msg.get("current_turn", -1)
                if turn == my_seat:
                    time.sleep(1.0)  # let human see the board
                    # Ask server for valid plays via HINT
                    send_message(sock, {"action": "HINT"})
                    hint_msg = None
                    deadline = time.time() + 3.0
                    while time.time() < deadline:
                        hint_msg = recv_message(sock)
                        if hint_msg and hint_msg.get("type") == "hint":
                            break
                    options = hint_msg.get("options", []) if hint_msg else []
                    if options:
                        cards = list(options[0])
                        send_message(sock, {"action": "PLAY", "cards": cards})
                        print(f"[Bot{bot_id}] seat={my_seat} PLAY {cards}")
                    else:
                        send_message(sock, {"action": "PASS"})
                        print(f"[Bot{bot_id}] seat={my_seat} PASS")

            elif state == "END":
                winner = msg.get("winner", -1)
                print(f"[Bot{bot_id}] seat={my_seat} GAME OVER winner=P{winner}")
                # Stay connected for room recycling

    except OSError as e:
        print(f"[Bot{bot_id}] disconnected: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    print("Starting 4 bot players...")
    threads = []
    for i in range(4):
        t = threading.Thread(target=bot_loop, args=(i,), daemon=True)
        t.start()
        threads.append(t)
        time.sleep(0.3)  # stagger so they fill the same room

    print("4 bots running. Press Ctrl+C to stop.")
    try:
        for t in threads:
            t.join()
    except KeyboardInterrupt:
        print("\nShutting down.")
        sys.exit(0)
