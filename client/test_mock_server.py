"""Mock game server for testing the NexusCore Windows client end-to-end."""
import socket
import struct
import json
import sys

HOST = "127.0.0.1"
PORT = 8080

# Initial game state (matches P5.mockState() format in p5_ui.html)
INITIAL_STATE = {
    "state": "PLAYING",
    "my_cards": [0, 1, 4, 5, 8, 9, 12, 13, 18, 22, 26, 30],
    "player_card_counts": [8, 14, 5, 10, 12],
    "current_turn": 4,
    "is_landlord": True,
    "landlords": [2, 4],
    "last_played": [],
    "last_player": -1,
    "bottom_cards": [48, 49, 51, 53],
    "multiplier": 512,
    "player_last_played": {
        "0": [0, 1],
        "2": [26]
    }
}

def send_message(sock: socket.socket, data: dict) -> None:
    """Send a JSON message with 4-byte big-endian length prefix."""
    body = json.dumps(data).encode("utf-8")
    header = struct.pack("!I", len(body))  # !I = network byte order (big-endian), 4 bytes
    sock.sendall(header + body)
    print(f"[mock-server] Sent {len(body)} bytes: {data.get('action', body.decode()[:60])}")

def recv_message(sock: socket.socket) -> dict:
    """Receive a 4-byte length-prefixed JSON message."""
    header = sock.recv(4)
    if len(header) < 4:
        return None
    body_len = struct.unpack("!I", header)[0]
    body = b""
    while len(body) < body_len:
        chunk = sock.recv(body_len - len(body))
        if not chunk:
            return None
        body += chunk
    return json.loads(body.decode("utf-8"))

def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(1)
    print(f"[mock-server] Listening on {HOST}:{PORT}")
    print(f"[mock-server] Waiting for nexus_client.exe to connect...")

    conn, addr = server.accept()
    print(f"[mock-server] Client connected from {addr}")

    # Send initial game state
    send_message(conn, {"type": "game_state", **INITIAL_STATE})
    print("[mock-server] Initial game state sent. UI should now show 12 cards + 5 players.")

    # Loop: receive player actions, respond with updated states
    seq = 0
    try:
        while True:
            msg = recv_message(conn)
            if msg is None:
                print("[mock-server] Client disconnected")
                break

            action = msg.get("action", "UNKNOWN")
            print(f"[mock-server] Received action: {msg}")

            # Simulate server response based on action
            if action == "PLAY":
                cards = msg.get("cards", [])
                print(f"  -> Player played cards: {cards}")
                # Remove played cards from hand for next state
                new_cards = [c for c in INITIAL_STATE["my_cards"] if c not in cards]
                updated = dict(INITIAL_STATE, my_cards=new_cards,
                               current_turn=(INITIAL_STATE["current_turn"] + 1) % 5,
                               player_last_played={str(INITIAL_STATE["current_turn"]): cards},
                               player_card_counts=[8, 14, 5, 10, len(new_cards)])
                send_message(conn, {"type": "game_state", **updated})
            elif action == "PASS":
                print("  -> Player passed")
                updated = dict(INITIAL_STATE,
                               current_turn=(INITIAL_STATE["current_turn"] + 1) % 5)
                send_message(conn, {"type": "game_state", **updated})
            elif action == "HINT":
                print("  -> Player requested hint (not implemented in mock)")
                send_message(conn, {"type": "game_state", **INITIAL_STATE})

            seq += 1

    except KeyboardInterrupt:
        print("\n[mock-server] Shutting down")
    finally:
        conn.close()
        server.close()

if __name__ == "__main__":
    main()
