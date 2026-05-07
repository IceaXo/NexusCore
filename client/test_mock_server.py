"""Full 5-player Dou Di Zhu game simulator for end-to-end client testing.

Uses the same 4-byte big-endian length-prefixed TCP protocol as the real server.
Simulates: DEAL → BIDDING → PLAYING → END, with auto-play for AI seats.

Usage: python test_mock_server.py
"""
import socket
import struct
import json
import random
import sys
import time
import io

# Force UTF-8 stdout to avoid GBK encoding errors on Windows
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

HOST = "127.0.0.1"
PORT = 8080

# Card encoding (matches server CardRule.h):
#   val/4 = logical rank: 0=3, 1=4, 2=5, ..., 9=Q, 10=K, 11=A, 12=2
#   val%4 = suit: 0=Diamond, 1=Club, 2=Heart, 3=Spade
#   53 = small joker, 56 = big joker
RANK_NAMES = ['3','4','5','6','7','8','9','10','J','Q','K','A','2']
SUIT_NAME  = ['♦','♣','♥','♠']

def card_name(v):
    if v == 53: return 'SJ'
    if v == 56: return 'BJ'
    return f'{SUIT_NAME[v%4]}{RANK_NAMES[v//4]}'

def card_rank(v):
    """Return logical rank: 3=0 .. 2=12, SJ=13, BJ=14"""
    if v == 53: return 13
    if v == 56: return 14
    return v // 4

def sort_hand(cards):
    """Sort by rank ascending, same rank by suit ascending."""
    return sorted(cards, key=lambda v: (card_rank(v), v % 4))


# ================================================================
# Game Engine
# ================================================================

class GameEngine:
    def __init__(self):
        self.state = "WAITING"
        self.players = [[] for _ in range(5)]   # hand cards
        self.bottom_cards = []                  # 4 bottom cards
        self.landlords = []                     # landlord indices
        self.current_turn = -1
        self.last_player = -1                   # who played the cards on table
        self.last_played = []                   # cards currently on table to beat
        self.last_played_type = None            # (type_str, body_rank) for comparison
        self.pass_count = 0
        self.multiplier = 1
        self.winner = -1
        self.phase_log = []                     # human-readable log
        self.player_last_played = {}            # idx -> last cards shown on avatar

        # Client is always seat 4
        self.CLIENT_SEAT = 4

    # --- Deck & deal ---
    def init_deck(self):
        """54 cards: 0-51 normal, 53 small joker, 56 big joker."""
        deck = list(range(52)) + [53, 56]
        random.shuffle(deck)
        return deck

    def deal(self):
        deck = self.init_deck()
        # Force both jokers into P4's hand for testing
        for jv in [53, 56]:
            if jv not in deck[40:50]:
                src = deck.index(jv)
                dst = random.choice([i for i in range(40, 50) if deck[i] not in [53, 56]])
                deck[src], deck[dst] = deck[dst], deck[src]
        for i in range(5):
            self.players[i] = sort_hand(deck[i*10 : (i+1)*10])
        self.bottom_cards = sort_hand(deck[50:54])
        self._log(f"Dealt 10 cards each + {len(self.bottom_cards)} bottom cards")
        for i, h in enumerate(self.players):
            self._log(f"  P{i}: {len(h)} cards -- {' '.join(card_name(c) for c in h)}")

    # --- Bidding ---
    def find_first_bidders(self):
        """Diamond-3 (val 0) and Diamond-4 (val 4) get first bid rights."""
        d3_holder = -1
        d4_holder = -1
        for i, hand in enumerate(self.players):
            for c in hand:
                if c == 0:  d3_holder = i
                if c == 4:  d4_holder = i
        return d3_holder, d4_holder

    def run_bidding(self):
        """Simulate bidding. Returns True if bidding succeeded."""
        if self.state != "WAITING":
            return True

        self.deal()
        d3, d4 = self.find_first_bidders()
        self.state = "BIDDING"
        self._log(f"BIDDING phase: Diamond-3 holder = P{d3}, Diamond-4 holder = P{d4}")

        # Auto-bid: both call (most common scenario)
        self.landlords = [d3, d4] if d3 != d4 else [d3]
        for ll in self.landlords:
            self._log(f"  P{ll} CALLS -- is now LANDLORD")

        # Distribute bottom cards
        if len(self.landlords) == 2:
            # Split: first 2 to first landlord, last 2 to second
            for i, ll in enumerate(self.landlords):
                bonus = self.bottom_cards[i*2:(i+1)*2]
                self.players[ll].extend(bonus)
                self.players[ll] = sort_hand(self.players[ll])
                self._log(f"  P{ll} gets bottom: {' '.join(card_name(c) for c in bonus)}")
        else:
            # Single landlord gets all 4
            self.players[self.landlords[0]].extend(self.bottom_cards)
            self.players[self.landlords[0]] = sort_hand(self.players[self.landlords[0]])

        # Enter PLAYING phase
        self.state = "PLAYING"
        self.current_turn = d3  # diamond-3 landlord plays first
        self.last_player = -1   # new round, free play
        self.last_played = []
        self.pass_count = 0
        self._log(f"PLAYING phase begins -- P{self.current_turn} plays first")
        return True

    # --- Playing ---
    def _rank(self, v):
        return card_rank(v)

    def _classify(self, cards):
        """Minimal classifier for auto-play comparison. Returns (type_str, body_rank, len)."""
        n = len(cards)
        if n == 0:
            return ('empty', 0, 0)
        ranks = sorted(set(self._rank(c) for c in cards))

        if n == 1:
            return ('single', ranks[0], 1)
        if n == 2 and ranks[0] == ranks[-1]:
            return ('pair', ranks[0], 2)
        # bomb: 3 or 4 same rank
        if n in (3, 4) and len(ranks) == 1:
            return ('bomb', ranks[0], n)
        # rocket: SJ + BJ
        if set(cards) == {53, 56}:
            return ('rocket', 999, 2)
        return ('other', min(ranks), n)

    def can_beat(self, my_type, my_rank, my_len, their_type, their_rank, their_len):
        """Check if my play beats the table."""
        # Rocket beats everything
        if my_type == 'rocket':
            return True
        if their_type == 'rocket':
            return False
        # Bomb beats non-bomb
        if my_type == 'bomb' and their_type != 'bomb':
            return True
        if their_type == 'bomb' and my_type != 'bomb':
            return False
        # Same type: compare power
        if my_type == their_type:
            if my_type == 'bomb':
                return my_len > their_len or (my_len == their_len and my_rank > their_rank)
            if my_type in ('single', 'pair'):
                return my_rank > their_rank
        return False

    def auto_play(self, seat):
        """AI decision for simulated players. Returns (cards_to_play, action)."""
        hand = self.players[seat]
        if not hand:
            return [], 'PASS'

        if self.last_player == -1 or self.last_player == seat:
            # Free play / new round: play lowest single
            return [hand[0]], 'PLAY'

        # Need to beat: try to find a higher single (simplest strategy)
        their_type, their_rank, their_len = self.last_played_type
        for c in hand:
            my_type, my_rank, my_len = self._classify([c])
            if self.can_beat(my_type, my_rank, my_len, their_type, their_rank, their_len):
                return [c], 'PLAY'

        # Can't beat: pass
        return [], 'PASS'

    def process_action(self, action, cards=None):
        """Process a player action. Returns True if game continues."""
        if self.state != "PLAYING":
            return False
        seat = self.current_turn

        if action == "PLAY" and cards and len(cards) > 0:
            # Remove cards from hand
            for c in cards:
                if c in self.players[seat]:
                    self.players[seat].remove(c)
                else:
                    self._log(f"  !! P{seat} tried to play card not in hand: {card_name(c)}")
                    return False

            self.last_played = cards
            self.last_player = seat
            self.last_played_type = self._classify(cards)
            self.pass_count = 0
            self.multiplier = max(self.multiplier, 1)

            # Record for UI display
            self.player_last_played[seat] = cards
            card_str = ' '.join(card_name(c) for c in cards)
            self._log(f"  P{seat} PLAYS [{card_str}]  (hand left: {len(self.players[seat])})")

            # Bomb multiplies
            if self.last_played_type[0] in ('bomb', 'rocket'):
                self.multiplier *= 2
                self._log(f"  !! BOMB/ROCKET! Multiplier x2 = {self.multiplier}")

            # Check win
            if len(self.players[seat]) == 0:
                self.state = "END"
                self.winner = seat
                self._log(f"  *** P{seat} wins! Multiplier: {self.multiplier} ***")
                return False  # game over

        elif action == "PASS":
            if self.last_player == -1:
                # First player of a new round can't pass
                self._log(f"  P{seat} can't PASS on a new round -- must play")
                return True

            self.pass_count += 1
            self._log(f"  P{seat} PASSES  (pass count: {self.pass_count}/4)")

            # 4 consecutive passes → reset: last player starts new round
            if self.pass_count >= 4:
                starter = self.last_player  # save before clearing
                self._log(f"  >> New round! P{starter} starts fresh")
                self.last_player = -1
                self.last_played = []
                self.last_played_type = None
                self.pass_count = 0
                self.current_turn = starter
                return True  # skip turn advance
                # Actually: the last player who played starts the new round
                # We need to fix this -- let me keep the old last_player before reset
                # Already handled above

        # Advance to next player with cards
        if self.state == "PLAYING":
            self._advance_turn()

        return True

    def _advance_turn(self):
        """Rotate to next player who still has cards. Skips empty-handed."""
        if self.state == "END":
            return
        start = self.current_turn
        while True:
            self.current_turn = (self.current_turn + 1) % 5
            if len(self.players[self.current_turn]) > 0:
                break
            if self.current_turn == start:
                break  # everyone empty (unlikely)

    # --- State serialization ---
    def serialize_for_player(self, seat):
        """Build the JSON state for a specific player (matching server format)."""
        is_ll = seat in self.landlords
        return {
            "state": self.state,
            "my_cards": self.players[seat][:],
            "player_card_counts": [len(self.players[i]) for i in range(5)],
            "current_turn": self.current_turn,
            "is_landlord": is_ll,
            "landlords": self.landlords,
            "last_played": self.last_played if self.last_player != -1 else [],
            "last_player": self.last_player,
            "bottom_cards": self.bottom_cards if is_ll else [],
            "multiplier": self.multiplier,
            "player_last_played": {str(k): v for k, v in self.player_last_played.items()},
            "winner": self.winner if self.state == "END" else None,
            "log": self.phase_log[-20:]   # last 20 log entries
        }

    def _log(self, msg):
        self.phase_log.append(msg)
        print(msg, flush=True)


# ================================================================
# Network I/O
# ================================================================

def send_message(sock, data):
    body = json.dumps(data).encode("utf-8")
    header = struct.pack("!I", len(body))
    sock.sendall(header + body)

def recv_message(sock):
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


# ================================================================
# Main
# ================================================================

def main():
    print("=" * 60)
    print("  NexusCore -- Full 5-Player Game Simulator")
    print("  Client seat: P4 (you)")
    print("=" * 60)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(1)
    print(f"\n[mock] Listening on {HOST}:{PORT} -- waiting for client...\n")

    conn, addr = server.accept()
    print(f"[mock] Client connected: {addr}\n")

    # --- Phase 1: Deal & Bidding ---
    engine = GameEngine()
    engine.run_bidding()

    # Send initial PLAYING state to client
    state = engine.serialize_for_player(engine.CLIENT_SEAT)
    send_message(conn, state)
    print(f"\n[mock] Initial PLAYING state sent to client (P4)")
    print(f"[mock] Your hand: {' '.join(card_name(c) for c in engine.players[4])}")
    print(f"[mock] Landlords: P{engine.landlords}")
    print(f"[mock] P{engine.current_turn}'s turn\n")

    # --- Phase 2: Playing loop ---
    try:
        while engine.state == "PLAYING":
            turn = engine.current_turn

            if turn == engine.CLIENT_SEAT:
                # Wait for client action
                print(f"[mock] Waiting for YOUR action (P4)...", flush=True)
                msg = recv_message(conn)
                if msg is None:
                    print("[mock] Client disconnected")
                    break

                action = msg.get("action", "PASS")
                cards = msg.get("cards", [])
                print(f"[mock] You: {action} {' '.join(card_name(c) for c in cards)}")
                engine.process_action(action, cards)

                # Send updated state back
                state = engine.serialize_for_player(engine.CLIENT_SEAT)
                send_message(conn, state)

            else:
                # Auto-play for AI seats
                time.sleep(0.3)  # brief delay for readability
                cards, action = engine.auto_play(turn)
                engine.process_action(action, cards)

                # Push state update to client
                state = engine.serialize_for_player(engine.CLIENT_SEAT)
                send_message(conn, state)

        # --- Phase 3: Game Over ---
        if engine.state == "END":
            time.sleep(0.5)
            state = engine.serialize_for_player(engine.CLIENT_SEAT)
            send_message(conn, state)
            print(f"\n{'='*60}")
            print(f"  GAME OVER -- P{engine.winner} wins!")
            print(f"  Multiplier: x{engine.multiplier}")
            print(f"{'='*60}\n")

            # Keep connection alive for a bit so client can display result
            time.sleep(5)

    except KeyboardInterrupt:
        print("\n[mock] Shutting down")
    except Exception as e:
        print(f"\n[mock] Error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        conn.close()
        server.close()

if __name__ == "__main__":
    main()
