#include "Room.h"
#include "CardRule.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <cstring>
#include <arpa/inet.h>
#include <iostream>

// ===================================================================
// PlayerContext::RemoveCards
// ===================================================================
void PlayerContext::RemoveCards(const std::vector<uint8_t>& cards) {
    for (uint8_t c : cards) {
        auto it = std::find(hand.begin(), hand.end(), c);
        if (it != hand.end()) {
            hand.erase(it);
        }
    }
}

// ===================================================================
// Room::GenerateToken
// ===================================================================
std::string Room::GenerateToken() {
    static const char hex[] = "0123456789abcdef";
    std::string token(8, '\0');
    std::mt19937 rng(std::random_device{}());
    for (int i = 0; i < 8; ++i) {
        token[i] = hex[rng() % 16];
    }
    return token;
}

// ===================================================================
// Room::ResetRoom —— 保留名字/头像，清除 ready，回 WAITING
// ===================================================================
void Room::ResetRoom() {
    for (int i = 0; i < 5; ++i) {
        // 保留 name、avatar、is_ai
        std::string saved_name = players[i].name;
        int saved_avatar = players[i].avatar;
        bool saved_is_ai = players[i].is_ai;
        players[i] = PlayerContext{};
        players[i].name = saved_name;
        players[i].avatar = saved_avatar;
        players[i].fd = -1;
        players[i].is_ai = saved_is_ai;
    }
    current_turn = 0;
    last_player_idx = -1;
    pass_count = 0;
    last_played_cards.clear();
    for (int i = 0; i < 5; ++i) player_last_played[i].clear();
    bottom_cards.clear();
    multiplier = 1;
    bidder_queue.clear();
    current_bidder_pos = 0;
    landlord_count = 0;
    bottom_pick_indices[0] = bottom_pick_indices[1] = -1;
    bottom_pick_count = 0;
    bottom_pick_landlord = -1;
    ai_scheduled_at = 0;
    for (int i = 0; i < 5; ++i) round_scores[i] = 0;
    // AI 玩家自动就绪
    for (int i = 0; i < 5; ++i) {
        if (players[i].is_ai) players[i].is_ready = true;
    }
    state = RoomState::WAITING;
}

// ===================================================================
// Room::ReturnToWaiting —— END → WAITING，保留 fd/名字/头像/总分
// ===================================================================
void Room::ReturnToWaiting() {
    for (int i = 0; i < 5; ++i) {
        players[i].hand.clear();
        players[i].is_landlord = false;
        players[i].has_passed_bidding = false;
        players[i].has_played = false;
        players[i].is_autoplay = false;
        // AI 或断线托管玩家自动就绪，真人重新等待准备
        if (players[i].is_ai || (players[i].fd == -1 && !players[i].name.empty())) {
            players[i].is_ready = true;
        } else {
            players[i].is_ready = false;
        }
        // 保留 fd, name, avatar, reconnect_token
    }
    current_turn = 0;
    last_player_idx = -1;
    pass_count = 0;
    last_played_cards.clear();
    for (int i = 0; i < 5; ++i) player_last_played[i].clear();
    bottom_cards.clear();
    multiplier = 1;
    bidder_queue.clear();
    current_bidder_pos = 0;
    landlord_count = 0;
    bottom_pick_indices[0] = bottom_pick_indices[1] = -1;
    bottom_pick_count = 0;
    bottom_pick_landlord = -1;
    ai_scheduled_at = 0;
    for (int i = 0; i < 5; ++i) round_scores[i] = 0;
    state = RoomState::WAITING;
}

// ===================================================================
// Room::FullReset —— 完全重置，清空名字头像总分
// ===================================================================
void Room::FullReset() {
    for (int i = 0; i < 5; ++i) {
        players[i] = PlayerContext{};
    }
    current_turn = 0;
    last_player_idx = -1;
    pass_count = 0;
    last_played_cards.clear();
    for (int i = 0; i < 5; ++i) player_last_played[i].clear();
    bottom_cards.clear();
    multiplier = 1;
    bidder_queue.clear();
    current_bidder_pos = 0;
    landlord_count = 0;
    bottom_pick_indices[0] = bottom_pick_indices[1] = -1;
    bottom_pick_count = 0;
    bottom_pick_landlord = -1;
    ai_scheduled_at = 0;
    for (int i = 0; i < 5; ++i) cumulative_scores[i] = 0;
    for (int i = 0; i < 5; ++i) round_scores[i] = 0;
    owner_seat = -1;
    total_rounds = 5;
    current_round = 0;
    state = RoomState::WAITING;
}

// ===================================================================
// Room::FindPlayerByToken
// ===================================================================
int Room::FindPlayerByToken(const std::string& token) const {
    if (token.empty()) return -1;
    for (int i = 0; i < 5; ++i) {
        if (players[i].reconnect_token == token && players[i].fd == -1) {
            return i;
        }
    }
    return -1;
}

// ===================================================================
// Room::ReconnectPlayer
// ===================================================================
void Room::ReconnectPlayer(int player_idx, int new_fd) {
    if (player_idx < 0 || player_idx >= 5) return;
    players[player_idx].fd = new_fd;
}

// ===================================================================
// Room::IsSeatOccupied —— 真人（fd!=-1）或 AI（fd==-1 但有名字）
// ===================================================================
bool Room::IsSeatOccupied(int seat) const {
    return players[seat].fd != -1 || players[seat].is_ai;
}

// ===================================================================
// Room::AllReady
// ===================================================================
bool Room::AllReady() const {
    for (int i = 0; i < 5; ++i) {
        if (players[i].fd == -1 && !players[i].name.empty()) {
            // AI 托管玩家：按 is_ready 判断
            if (!players[i].is_ready) return false;
        } else if (players[i].fd == -1) {
            // 空位：不能开局
            return false;
        } else {
            // 真人玩家：必须 ready
            if (!players[i].is_ready) return false;
        }
    }
    return true;
}

// ===================================================================
// Room::TransferOwnership
// ===================================================================
void Room::TransferOwnership() {
    for (int i = 0; i < 5; ++i) {
        int seat = (owner_seat + 1 + i) % 5;
        if (players[seat].fd != -1) {
            owner_seat = seat;
            return;
        }
    }
    owner_seat = -1; // 没人了
}

// ===================================================================
// Room::GetFirstLandlord / GetSecondLandlord
// ===================================================================
int Room::GetFirstLandlord() const {
    std::vector<int> landlord_indices;
    for (int i = 0; i < 5; ++i) {
        if (players[i].is_landlord) landlord_indices.push_back(i);
    }
    if (landlord_indices.size() < 2) return -1;

    auto find_min_diamond = [](const std::vector<uint8_t>& hand) -> int {
        int min_rank = 999;
        for (uint8_t c : hand) {
            if (CardRule::IsNormalCard(c) && CardRule::GetSuit(c) == 0) {
                int r = CardRule::GetRank(c);
                if (r < min_rank) min_rank = r;
            }
        }
        return min_rank;
    };

    return find_min_diamond(players[landlord_indices[0]].hand) <
           find_min_diamond(players[landlord_indices[1]].hand)
           ? landlord_indices[0] : landlord_indices[1];
}

int Room::GetSecondLandlord() const {
    std::vector<int> landlord_indices;
    for (int i = 0; i < 5; ++i) {
        if (players[i].is_landlord) landlord_indices.push_back(i);
    }
    if (landlord_indices.size() < 2) return -1;
    int first = GetFirstLandlord();
    return (landlord_indices[0] == first) ? landlord_indices[1] : landlord_indices[0];
}

// ===================================================================
// Room::CheckSpring
// ===================================================================
bool Room::CheckSpring() const {
    for (int i = 0; i < 5; ++i) {
        if (!players[i].is_landlord && players[i].has_played) {
            return false;
        }
    }
    return true;
}

// ===================================================================
// Room::GetPlayerIndex
// ===================================================================
int Room::GetPlayerIndex(int fd) const {
    for (int i = 0; i < 5; ++i) {
        if (players[i].fd == fd) return i;
    }
    return -1;
}

// ===================================================================
// SendError
// ===================================================================
static void SendError(const Room::SendCallback& on_send, int fd,
                      const char* code, const char* msg) {
    if (!on_send) return;
    std::ostringstream ss;
    ss << "{\"type\":\"error\",\"code\":\"" << code
       << "\",\"message\":\"" << msg << "\"}";
    on_send(fd, ss.str());
}

// ===================================================================
// Room::DealCards
// ===================================================================
void Room::DealCards(std::mt19937& rng) {
    std::vector<uint8_t> deck;
    deck.reserve(54);
    for (int i = 0; i <= 51; ++i) deck.push_back(static_cast<uint8_t>(i));
    deck.push_back(53);
    deck.push_back(56);

    std::shuffle(deck.begin(), deck.end(), rng);

    for (int i = 0; i < 5; ++i) {
        players[i].hand.clear();
        for (int j = 0; j < 10; ++j) {
            players[i].hand.push_back(deck[i * 10 + j]);
        }
        CardRule::SortHand(players[i].hand);
    }

    bottom_cards.clear();
    for (int i = 50; i < 54; ++i) {
        bottom_cards.push_back(deck[i]);
    }
}

// ===================================================================
// Room::BuildBidderQueue
// ===================================================================
void Room::BuildBidderQueue() {
    bidder_queue.clear();
    for (int rank = 0; rank <= 12; ++rank) {
        for (int i = 0; i < 5; ++i) {
            bool found = false;
            for (uint8_t c : players[i].hand) {
                if (CardRule::IsNormalCard(c) && CardRule::GetSuit(c) == 0 && CardRule::GetRank(c) == rank) {
                    if (std::find(bidder_queue.begin(), bidder_queue.end(), i) == bidder_queue.end()) {
                        bidder_queue.push_back(i);
                    }
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }
    current_bidder_pos = 0;
}

// ===================================================================
// Room::StartGame
// ===================================================================
void Room::StartGame() {
    std::mt19937 rng(std::random_device{}());
    DealCards(rng);
    BuildBidderQueue();

    current_round++;

    for (int i = 0; i < 5; ++i) {
        players[i].is_landlord = false;
        players[i].has_passed_bidding = false;
        players[i].has_played = false;
        players[i].is_ready = false;
        players[i].reconnect_token = GenerateToken();
        round_scores[i] = 0;
    }
    landlord_count = 0;
    current_bidder_pos = 0;
    multiplier = 1;
    bottom_pick_indices[0] = bottom_pick_indices[1] = -1;
    bottom_pick_count = 0;
    bottom_pick_landlord = -1;
    ai_scheduled_at = 0;

    state = RoomState::BIDDING;
    BroadcastState();
}

// ===================================================================
// Room::HandleBidding
// ===================================================================
void Room::HandleBidding(int fd, const std::string& json) {
    if (state != RoomState::BIDDING) return;

    int idx = GetPlayerIndex(fd);
    if (idx == -1) return;

    if (current_bidder_pos >= static_cast<int>(bidder_queue.size())) return;
    int expected_bidder = bidder_queue[current_bidder_pos];
    if (idx != expected_bidder) {
        SendError(on_send, fd, "NOT_YOUR_BIDDING", "还没轮到你叫地主");
        return;
    }

    bool is_call = (json.find("\"CALL\"") != std::string::npos ||
                    json.find("\"action\":\"CALL\"") != std::string::npos);
    bool is_pass = (json.find("\"PASS\"") != std::string::npos ||
                    json.find("\"action\":\"PASS\"") != std::string::npos);

    if (is_call) {
        players[idx].is_landlord = true;
        landlord_count++;
    } else if (is_pass) {
        players[idx].has_passed_bidding = true;
    } else {
        SendError(on_send, fd, "BAD_BID_ACTION", "叫地主只能 CALL 或 PASS");
        return;
    }

    current_bidder_pos++;

    if (landlord_count >= 2) {
        // 凑齐 2 个地主 → 进入底牌选择阶段
        state = RoomState::BOTTOM_PICK;
        bottom_pick_count = 0;
        bottom_pick_indices[0] = bottom_pick_indices[1] = -1;
        bottom_pick_landlord = GetFirstLandlord();
    } else if (current_bidder_pos >= static_cast<int>(bidder_queue.size())) {
        // 所有人都不叫 → 重新洗牌发牌
        StartGame();
        return;
    }

    BroadcastState();
}

// ===================================================================
// Room::HandlePickBottom —— 底牌选择阶段
// ===================================================================
void Room::HandlePickBottom(int fd, const std::string& json) {
    if (state != RoomState::BOTTOM_PICK) return;

    int idx = GetPlayerIndex(fd);
    if (idx == -1) return;
    if (idx != bottom_pick_landlord) {
        SendError(on_send, fd, "NOT_YOUR_PICK", "不是你在选底牌");
        return;
    }

    // 解析 indices 数组
    std::vector<int> indices;
    size_t pos = json.find("\"indices\"");
    if (pos != std::string::npos) {
        pos = json.find('[', pos);
        if (pos != std::string::npos) {
            size_t end = json.find(']', pos);
            if (end != std::string::npos) {
                std::string arr = json.substr(pos + 1, end - pos - 1);
                size_t start = 0;
                while (start < arr.size()) {
                    while (start < arr.size() && (arr[start] == ',' || arr[start] == ' ')) start++;
                    if (start >= arr.size()) break;
                    size_t num_end = start;
                    while (num_end < arr.size() && arr[num_end] >= '0' && arr[num_end] <= '9') num_end++;
                    if (num_end > start) {
                        indices.push_back(std::stoi(arr.substr(start, num_end - start)));
                    }
                    start = num_end;
                }
            }
        }
    }

    if (indices.size() != 2) {
        SendError(on_send, fd, "BAD_PICK", "必须选 2 张底牌");
        return;
    }

    // 验证索引范围
    for (int i : indices) {
        if (i < 0 || i >= 4) {
            SendError(on_send, fd, "BAD_INDEX", "底牌索引必须在 0-3");
            return;
        }
    }
    if (indices[0] == indices[1]) {
        SendError(on_send, fd, "DUPLICATE_INDEX", "不能选同一张底牌");
        return;
    }

    // 记录选择
    bottom_pick_indices[0] = indices[0];
    bottom_pick_indices[1] = indices[1];
    bottom_pick_count = 2;

    // 分出选中的和不选的底牌
    std::vector<uint8_t> picked = {bottom_cards[indices[0]], bottom_cards[indices[1]]};
    std::vector<uint8_t> remaining;
    for (int i = 0; i < 4; ++i) {
        if (i != indices[0] && i != indices[1]) {
            remaining.push_back(bottom_cards[i]);
        }
    }

    DistributeBottomCardsAfterPick(picked, remaining);

    // 进入出牌阶段
    state = RoomState::PLAYING;
    int first_landlord = GetFirstLandlord();
    current_turn = first_landlord;
    last_player_idx = -1;
    last_played_cards.clear();
    pass_count = 0;

    BroadcastState();
}

// ===================================================================
// Room::DistributeBottomCardsAfterPick
// ===================================================================
void Room::DistributeBottomCardsAfterPick(const std::vector<uint8_t>& picked,
                                           const std::vector<uint8_t>& remaining) {
    int first = GetFirstLandlord();
    int second = GetSecondLandlord();

    // 地主A（方块小的，先选）拿 picked
    auto& hand_first = players[first].hand;
    hand_first.insert(hand_first.end(), picked.begin(), picked.end());
    CardRule::SortHand(hand_first);

    // 地主B 拿剩下的
    if (second != -1) {
        auto& hand_second = players[second].hand;
        hand_second.insert(hand_second.end(), remaining.begin(), remaining.end());
        CardRule::SortHand(hand_second);
    }

    // 保留底牌用于序列化
    std::vector<uint8_t> all_bottom;
    all_bottom.insert(all_bottom.end(), picked.begin(), picked.end());
    all_bottom.insert(all_bottom.end(), remaining.begin(), remaining.end());
    bottom_cards = all_bottom;
}

// ===================================================================
// Room::HandlePlaying
// ===================================================================
void Room::HandlePlaying(int fd, const std::string& json) {
    if (state != RoomState::PLAYING) return;

    int idx = GetPlayerIndex(fd);
    if (idx == -1) return;
    if (idx != current_turn) {
        SendError(on_send, fd, "NOT_YOUR_TURN", "还没轮到你出牌");
        return;
    }
    if (players[idx].IsHandEmpty()) return;

    bool is_pass = (json.find("\"PASS\"") != std::string::npos ||
                    json.find("\"action\":\"PASS\"") != std::string::npos);
    bool is_play = (json.find("\"PLAY\"") != std::string::npos ||
                    json.find("\"action\":\"PLAY\"") != std::string::npos);

    if (is_pass) {
        if (last_player_idx == -1) {
            SendError(on_send, fd, "CANNOT_PASS", "新一轮出牌不能跳过");
            return;
        }

        pass_count++;
        if (pass_count >= 4) {
            current_turn = last_player_idx;
            last_player_idx = -1;
            last_played_cards.clear();
            pass_count = 0;
            BroadcastState();
            return;
        }
    } else if (is_play) {
        std::vector<uint8_t> play_cards;
        size_t pos = json.find("\"cards\"");
        if (pos == std::string::npos) {
            SendError(on_send, fd, "MISSING_CARDS", "缺少 cards 字段");
            return;
        }
        pos = json.find('[', pos);
        if (pos == std::string::npos) {
            SendError(on_send, fd, "BAD_JSON", "cards 字段格式错误");
            return;
        }
        size_t end = json.find(']', pos);
        if (end == std::string::npos) {
            SendError(on_send, fd, "BAD_JSON", "cards 字段格式错误");
            return;
        }

        std::string cards_str = json.substr(pos + 1, end - pos - 1);
        size_t start = 0;
        while (start < cards_str.size()) {
            while (start < cards_str.size() &&
                   (cards_str[start] == ',' || cards_str[start] == ' ')) {
                start++;
            }
            if (start >= cards_str.size()) break;
            size_t num_end = start;
            while (num_end < cards_str.size() && cards_str[num_end] >= '0' && cards_str[num_end] <= '9') {
                num_end++;
            }
            if (num_end > start) {
                int val = std::stoi(cards_str.substr(start, num_end - start));
                play_cards.push_back(static_cast<uint8_t>(val));
            }
            start = num_end;
        }

        if (play_cards.empty()) {
            SendError(on_send, fd, "EMPTY_CARDS", "不能出空牌");
            return;
        }

        for (uint8_t c : play_cards) {
            if (std::find(players[idx].hand.begin(), players[idx].hand.end(), c) == players[idx].hand.end()) {
                SendError(on_send, fd, "CARD_NOT_IN_HAND", "出了手里没有的牌");
                return;
            }
        }

        CardType type = CardRule::EvaluateType(play_cards);
        if (type == CardType::INVALID) {
            SendError(on_send, fd, "INVALID_PATTERN", "牌型不合法");
            return;
        }

        if (last_player_idx != -1) {
            if (!CardRule::CanBeat(play_cards, last_played_cards)) {
                SendError(on_send, fd, "CANNOT_BEAT", "打不过桌面上的牌");
                return;
            }
        }

        if (type == CardType::BOMB) {
            if (play_cards.size() == 3) {
                multiplier *= 2;
            } else if (play_cards.size() == 4) {
                multiplier *= 4;
            }
        } else if (type == CardType::ROCKET) {
            multiplier *= 4;
        }

        players[idx].RemoveCards(play_cards);
        players[idx].has_played = true;
        player_last_played[idx] = play_cards;
        last_played_cards = play_cards;
        last_player_idx = idx;
        pass_count = 0;
    } else {
        SendError(on_send, fd, "UNKNOWN_ACTION", "无效的操作类型");
        return;
    }

    if (players[idx].IsHandEmpty()) {
        if (players[idx].is_landlord && CheckSpring()) {
            multiplier *= 2;
        }
        state = RoomState::END;
        BroadcastState();
        return;
    }

    AdvanceToNextPlayer();
    BroadcastState();
}

// ===================================================================
// Room::HandleHint
// ===================================================================
void Room::HandleHint(int fd) {
    if (state != RoomState::PLAYING) return;
    int idx = GetPlayerIndex(fd);
    if (idx == -1) return;
    if (idx != current_turn) return;
    if (players[idx].IsHandEmpty()) return;

    auto options = CardRule::GetHints(players[idx].hand, last_played_cards);

    std::ostringstream ss;
    ss << "{\"type\":\"hint\",\"options\":[";
    for (size_t i = 0; i < options.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "[";
        for (size_t j = 0; j < options[i].size(); ++j) {
            if (j > 0) ss << ",";
            ss << static_cast<int>(options[i][j]);
        }
        ss << "]";
    }
    ss << "]}";

    if (on_send) on_send(fd, ss.str());
}

// ===================================================================
// Room::AdvanceToNextPlayer
// ===================================================================
void Room::AdvanceToNextPlayer() {
    int start = current_turn;
    do {
        current_turn = (current_turn + 1) % 5;
        if (!players[current_turn].IsHandEmpty()) return;
    } while (current_turn != start);
}

// ===================================================================
// Room::SetAITakeover
// ===================================================================
void Room::SetAITakeover(int player_idx) {
    if (player_idx < 0 || player_idx >= 5) return;
    players[player_idx].fd = -1;
}

// ===================================================================
// Room::HandleAIBidding
// ===================================================================
void Room::HandleAIBidding(int player_idx) {
    if (state != RoomState::BIDDING) return;
    if (current_bidder_pos >= static_cast<int>(bidder_queue.size())) return;
    if (bidder_queue[current_bidder_pos] != player_idx) return;

    bool is_last_bidder = (current_bidder_pos + 1 >= static_cast<int>(bidder_queue.size()));

    // 概率叫地主：没人叫时 70%，已有 1 人时 30%
    // 如果没地主且是最后一个 bidder，必须叫（避免无限重开）
    bool must_call = (landlord_count == 0 && is_last_bidder);
    int threshold = (landlord_count == 0) ? 70 : 30;
    std::mt19937 rng(std::random_device{}());

    if (must_call || (rng() % 100 < threshold)) {
        players[player_idx].is_landlord = true;
        landlord_count++;
    } else {
        players[player_idx].has_passed_bidding = true;
    }

    current_bidder_pos++;

    if (landlord_count >= 2) {
        state = RoomState::BOTTOM_PICK;
        bottom_pick_count = 0;
        bottom_pick_indices[0] = bottom_pick_indices[1] = -1;
        bottom_pick_landlord = GetFirstLandlord();
    } else if (current_bidder_pos >= static_cast<int>(bidder_queue.size())) {
        StartGame();
        return;
    }

    BroadcastState();
}

// ===================================================================
// Room::HandleAIPickBottom —— AI 在底牌选择阶段随机选 2 张
// ===================================================================
void Room::HandleAIPickBottom(int player_idx) {
    if (state != RoomState::BOTTOM_PICK) return;
    if (player_idx != bottom_pick_landlord) return;

    // AI 随机选 2 张
    std::mt19937 rng(std::random_device{}());
    int i0 = rng() % 4;
    int i1 = (i0 + 1 + (rng() % 3)) % 4;

    bottom_pick_indices[0] = i0;
    bottom_pick_indices[1] = i1;
    bottom_pick_count = 2;

    std::vector<uint8_t> picked = {bottom_cards[i0], bottom_cards[i1]};
    std::vector<uint8_t> remaining;
    for (int i = 0; i < 4; ++i) {
        if (i != i0 && i != i1) {
            remaining.push_back(bottom_cards[i]);
        }
    }

    DistributeBottomCardsAfterPick(picked, remaining);

    state = RoomState::PLAYING;
    int first_landlord = GetFirstLandlord();
    current_turn = first_landlord;
    last_player_idx = -1;
    last_played_cards.clear();
    pass_count = 0;

    BroadcastState();
}

// ===================================================================
// Room::ExecuteAIDecision
// ===================================================================
void Room::ExecuteAIDecision(int player_idx, const std::string& action,
                              const std::vector<uint8_t>& cards) {
    if (state != RoomState::PLAYING) return;
    if (player_idx != current_turn) return;
    if (players[player_idx].IsHandEmpty()) return;

    if (action == "PASS") {
        if (last_player_idx == -1) return;

        pass_count++;
        if (pass_count >= 4) {
            current_turn = last_player_idx;
            last_player_idx = -1;
            last_played_cards.clear();
            pass_count = 0;
            BroadcastState();
            return;
        }
    } else if (action == "PLAY") {
        if (cards.empty()) return;

        for (uint8_t c : cards) {
            if (std::find(players[player_idx].hand.begin(),
                          players[player_idx].hand.end(), c) == players[player_idx].hand.end()) {
                return;
            }
        }

        CardType type = CardRule::EvaluateType(cards);
        if (type == CardType::INVALID) return;

        if (last_player_idx != -1) {
            if (!CardRule::CanBeat(cards, last_played_cards)) return;
        }

        if (type == CardType::BOMB) {
            if (cards.size() == 3) multiplier *= 2;
            else if (cards.size() == 4) multiplier *= 4;
        } else if (type == CardType::ROCKET) {
            multiplier *= 4;
        }

        players[player_idx].RemoveCards(cards);
        players[player_idx].has_played = true;
        player_last_played[player_idx] = cards;
        last_played_cards = cards;
        last_player_idx = player_idx;
        pass_count = 0;
    } else {
        return;
    }

    if (players[player_idx].IsHandEmpty()) {
        if (players[player_idx].is_landlord && CheckSpring()) {
            multiplier *= 2;
        }
        state = RoomState::END;
        BroadcastState();
        return;
    }

    AdvanceToNextPlayer();
    BroadcastState();
}

// ===================================================================
// Room::SerializeState
// ===================================================================
std::string Room::SerializeState(int player_idx) const {
    std::ostringstream ss;
    ss << "{";

    const char* state_str = "WAITING";
    switch (state) {
        case RoomState::BIDDING:     state_str = "BIDDING"; break;
        case RoomState::BOTTOM_PICK: state_str = "BOTTOM_PICK"; break;
        case RoomState::PLAYING:     state_str = "PLAYING"; break;
        case RoomState::END:         state_str = "END"; break;
        default: break;
    }
    ss << "\"state\":\"" << state_str << "\"";

    // my_seat
    ss << ",\"my_seat\":" << player_idx;

    // ---- WAITING 状态：完整房间状态 ----
    if (state == RoomState::WAITING) {
        ss << ",\"owner_seat\":" << owner_seat;
        ss << ",\"total_rounds\":" << total_rounds;
        ss << ",\"current_round\":" << current_round;

        // player_names
        ss << ",\"player_names\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            ss << "\"" << players[i].name << "\"";
        }
        ss << "]";

        // player_avatars
        ss << ",\"player_avatars\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            ss << players[i].avatar;
        }
        ss << "]";

        // ready_states
        ss << ",\"ready_states\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            ss << (players[i].is_ready ? "true" : "false");
        }
        ss << "]";

        // cumulative_scores
        ss << ",\"cumulative_scores\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            ss << cumulative_scores[i];
        }
        ss << "]";

        // 每个座位的在线状态
        ss << ",\"player_online\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            ss << (players[i].fd != -1 ? "true" : "false");
        }
        ss << "]";

        // reconnect_token
        ss << ",\"reconnect_token\":\"" << players[player_idx].reconnect_token << "\"";

        ss << "}";
        return ss.str();
    }

    // ---- 非 WAITING 状态：游戏数据 ----

    // my_cards
    ss << ",\"my_cards\":[";
    const auto& hand = players[player_idx].hand;
    for (size_t i = 0; i < hand.size(); ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(hand[i]);
    }
    ss << "]";

    // player_card_counts
    ss << ",\"player_card_counts\":[";
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(players[i].hand.size());
    }
    ss << "]";

    // current_turn
    ss << ",\"current_turn\":" << current_turn;

    // is_landlord
    ss << ",\"is_landlord\":" << (players[player_idx].is_landlord ? "true" : "false");

    // landlords
    ss << ",\"landlords\":[";
    bool first_l = true;
    for (int i = 0; i < 5; ++i) {
        if (players[i].is_landlord) {
            if (!first_l) ss << ",";
            ss << i;
            first_l = false;
        }
    }
    ss << "]";

    // player_last_played
    ss << ",\"player_last_played\":{";
    bool first_plp = true;
    for (int i = 0; i < 5; ++i) {
        if (player_last_played[i].empty()) continue;
        if (!first_plp) ss << ",";
        ss << "\"" << i << "\":[";
        for (size_t j = 0; j < player_last_played[i].size(); ++j) {
            if (j > 0) ss << ",";
            ss << static_cast<int>(player_last_played[i][j]);
        }
        ss << "]";
        first_plp = false;
    }
    ss << "}";

    // last_played
    ss << ",\"last_played\":[";
    for (size_t i = 0; i < last_played_cards.size(); ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(last_played_cards[i]);
    }
    ss << "]";

    // last_played_type
    ss << ",\"last_played_type\":" << static_cast<int>(CardRule::EvaluateType(last_played_cards));

    // last_player
    ss << ",\"last_player\":" << last_player_idx;

    // multiplier
    ss << ",\"multiplier\":" << multiplier;

    // reconnect_token
    ss << ",\"reconnect_token\":\"" << players[player_idx].reconnect_token << "\"";

    // has_played
    ss << ",\"has_played\":" << (players[player_idx].has_played ? "true" : "false");

    // is_autoplay — 客户端据此同步 UI
    ss << ",\"is_autoplay\":" << (players[player_idx].is_autoplay ? "true" : "false");

    // player_names / player_avatars (all states)
    ss << ",\"player_names\":[";
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << players[i].name << "\"";
    }
    ss << "]";

    ss << ",\"player_avatars\":[";
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ss << ",";
        ss << players[i].avatar;
    }
    ss << "]";

    // current_round / total_rounds
    ss << ",\"current_round\":" << current_round;
    ss << ",\"total_rounds\":" << total_rounds;

    // cumulative_scores
    ss << ",\"cumulative_scores\":[";
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ss << ",";
        ss << cumulative_scores[i];
    }
    ss << "]";

    // owner_seat
    ss << ",\"owner_seat\":" << owner_seat;

    // BIDDING 阶段额外字段
    if (state == RoomState::BIDDING) {
        ss << ",\"first_bidder\":" << (bidder_queue.empty() ? -1 : bidder_queue[0]);
        ss << ",\"second_bidder\":" << (bidder_queue.size() > 1 ? static_cast<int>(bidder_queue[1]) : -1);
        ss << ",\"current_bidder\":" << (current_bidder_pos < static_cast<int>(bidder_queue.size())
                                          ? bidder_queue[current_bidder_pos] : -1);
    }

    // BOTTOM_PICK 阶段额外字段
    if (state == RoomState::BOTTOM_PICK) {
        // 地主A（选牌者）不能看底牌值，只发数量
        ss << ",\"bottom_cards_count\":" << bottom_cards.size();
        ss << ",\"bottom_pick_landlord\":" << bottom_pick_landlord;
        // 只有地主A能看到这个
        ss << ",\"is_picking\":" << (player_idx == bottom_pick_landlord ? "true" : "false");
        // 所选索引（选了之后才发送）
        if (bottom_pick_count > 0) {
            ss << ",\"bottom_pick_indices\":[" << bottom_pick_indices[0] << "," << bottom_pick_indices[1] << "]";
        }
    }

    // PLAYING/END 阶段底牌可见（发真实卡牌值）
    if (state == RoomState::PLAYING || state == RoomState::END) {
        ss << ",\"bottom_cards\":[";
        for (size_t i = 0; i < bottom_cards.size(); ++i) {
            if (i > 0) ss << ",";
            ss << static_cast<int>(bottom_cards[i]);
        }
        ss << "]";
    }

    // END 阶段额外字段
    if (state == RoomState::END) {
        for (int i = 0; i < 5; ++i) {
            if (players[i].IsHandEmpty()) {
                ss << ",\"winner\":" << i;
                ss << ",\"winner_is_landlord\":" << (players[i].is_landlord ? "true" : "false");
                break;
            }
        }

        // 结算分数
        bool landlord_win = false;
        for (int i = 0; i < 5; ++i) {
            if (players[i].IsHandEmpty() && players[i].is_landlord) {
                landlord_win = true;
                break;
            }
        }

        int64_t base = multiplier;
        ss << ",\"round_scores\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            int64_t score = 0;
            if (landlord_win) {
                score = players[i].is_landlord ? (3 * base) : (-2 * base);
            } else {
                score = players[i].is_landlord ? (-3 * base) : (2 * base);
            }
            ss << score;
        }
        ss << "]";

        // cumulative_scores (本局结束后的累计)
        ss << ",\"cumulative_scores\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            int64_t cum = cumulative_scores[i];
            if (landlord_win) {
                cum += players[i].is_landlord ? (3 * base) : (-2 * base);
            } else {
                cum += players[i].is_landlord ? (-3 * base) : (2 * base);
            }
            ss << cum;
        }
        ss << "]";

        ss << ",\"current_round\":" << current_round;
        ss << ",\"total_rounds\":" << total_rounds;
    }

    // 每个座位的在线状态
    ss << ",\"player_online\":[";
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ss << ",";
        ss << (players[i].fd != -1 ? "true" : "false");
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

// ===================================================================
// Room::SerializeRoomInfo —— 给大厅玩家的房间摘要
// ===================================================================
std::string Room::SerializeRoomInfo(int room_id) const {
    int count = 0;
    for (int i = 0; i < 5; ++i) {
        if (IsSeatOccupied(i)) count++;
    }

    std::ostringstream ss;
    ss << "{\"room_id\":" << room_id
       << ",\"player_count\":" << count
       << ",\"state\":\"" << (state == RoomState::WAITING ? "WAITING" : "PLAYING") << "\""
       << "}";
    return ss.str();
}

// ===================================================================
// Room::BroadcastState
// ===================================================================
void Room::BroadcastState() {
    if (!on_send) return;
    for (int i = 0; i < 5; ++i) {
        if (players[i].fd != -1) {
            on_send(players[i].fd, SerializeState(i));
        }
    }
}
