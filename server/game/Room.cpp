#include "Room.h"
#include "CardRule.h"
#include <algorithm>   // std::shuffle, std::find, std::sort
#include <random>     // std::mt19937, std::random_device
#include <sstream>    // std::ostringstream
#include <cstring>    // std::memcpy
#include <arpa/inet.h> // htonl

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
// Room::ResetRoom
// ===================================================================
void Room::ResetRoom() {
    for (int i = 0; i < 5; ++i) {
        players[i] = PlayerContext{};  // 清零所有字段，fd 变回 -1
    }
    current_turn = 0;
    last_player_idx = -1;
    pass_count = 0;
    last_played_cards.clear();
    bottom_cards.clear();
    multiplier = 1;
    bidder_queue.clear();
    current_bidder_pos = 0;
    landlord_count = 0;
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
// Room::CheckSpring
// ===================================================================
bool Room::CheckSpring() const {
    for (int i = 0; i < 5; ++i) {
        if (!players[i].is_landlord && players[i].has_played) {
            return false;  // 有农民出过牌，不是春天
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
// Room::DealCards
// ===================================================================
void Room::DealCards(std::mt19937& rng) {
    // 一副 54 张: 0..51 (普通) + 53 (小王) + 56 (大王)
    std::vector<uint8_t> deck;
    deck.reserve(54);
    for (int i = 0; i <= 51; ++i) deck.push_back(static_cast<uint8_t>(i));
    deck.push_back(53);  // 小王
    deck.push_back(56);  // 大王

    std::shuffle(deck.begin(), deck.end(), rng);

    // 每人 10 张
    for (int i = 0; i < 5; ++i) {
        players[i].hand.clear();
        for (int j = 0; j < 10; ++j) {
            players[i].hand.push_back(deck[i * 10 + j]);
        }
        CardRule::SortHand(players[i].hand);
    }

    // 底牌 4 张
    bottom_cards.clear();
    for (int i = 50; i < 54; ++i) {
        bottom_cards.push_back(deck[i]);
    }
}

// ===================================================================
// Room::BuildBidderQueue —— 按方块点数从小到大找持有者，去重形成队列
// ===================================================================
void Room::BuildBidderQueue() {
    bidder_queue.clear();
    // 遍历方块 3→4→5→...→A→2，即 rank 0..12
    for (int rank = 0; rank <= 12; ++rank) {
        // 方块牌编码 = rank*4 + 0
        uint8_t diamond_card = static_cast<uint8_t>(rank * 4);
        for (int i = 0; i < 5; ++i) {
            bool found = false;
            for (uint8_t c : players[i].hand) {
                if (c == diamond_card) {
                    // 该玩家尚未在队列中 → 加入
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
// Room::StartGame —— 洗牌发牌，构建叫地主队列，状态切 BIDDING
// ===================================================================
void Room::StartGame() {
    std::mt19937 rng(std::random_device{}());
    DealCards(rng);
    BuildBidderQueue();

    // 重置叫地主状态
    for (int i = 0; i < 5; ++i) {
        players[i].is_landlord = false;
        players[i].has_passed_bidding = false;
        players[i].has_played = false;
        players[i].reconnect_token = GenerateToken();
    }
    landlord_count = 0;
    current_bidder_pos = 0;
    multiplier = 1;

    state = RoomState::BIDDING;
    BroadcastState();
}

// ===================================================================
// Room::HandleBidding —— 按 bidder_queue 动态顺位叫地主
// ===================================================================
void Room::HandleBidding(int fd, const std::string& action) {
    if (state != RoomState::BIDDING) return;

    int idx = GetPlayerIndex(fd);
    if (idx == -1) return;

    // 检查是否轮到该玩家表态
    if (current_bidder_pos >= static_cast<int>(bidder_queue.size())) return;
    int expected_bidder = bidder_queue[current_bidder_pos];
    if (idx != expected_bidder) return;

    if (action == "CALL") {
        players[idx].is_landlord = true;
        landlord_count++;
    } else if (action == "PASS") {
        players[idx].has_passed_bidding = true;
    } else {
        return; // 未知 action，忽略
    }

    current_bidder_pos++;

    // 检查叫地主是否结束
    if (landlord_count >= 2) {
        // 凑齐 2 个地主 → 进入出牌阶段
        DistributeBottomCards();

        state = RoomState::PLAYING;
        // 持有更小方块的地主先手（bidder_queue 中靠前的）
        for (int candidate : bidder_queue) {
            if (players[candidate].is_landlord) {
                current_turn = candidate;
                break;
            }
        }
        last_player_idx = -1;
        last_played_cards.clear();
        pass_count = 0;
    } else if (current_bidder_pos >= static_cast<int>(bidder_queue.size())) {
        // 所有人（队列遍历完）都不叫 → 重新洗牌发牌
        StartGame();
        return;
    }

    BroadcastState();
}

// ===================================================================
// Room::DistributeBottomCards —— 底牌随机分两份，方块小的地主先选
// ===================================================================
void Room::DistributeBottomCards() {
    if (bottom_cards.size() != 4) return;

    // 随机分成两堆（各 2 张）
    std::mt19937 rng(std::random_device{}());
    auto shuffled = bottom_cards;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    std::vector<uint8_t> pile_a = {shuffled[0], shuffled[1]};
    std::vector<uint8_t> pile_b = {shuffled[2], shuffled[3]};

    // 找到两个地主，排序（方块更小者优先）
    std::vector<int> landlord_indices;
    for (int i = 0; i < 5; ++i) {
        if (players[i].is_landlord) landlord_indices.push_back(i);
    }
    // 按持有的最小方块排序
    std::sort(landlord_indices.begin(), landlord_indices.end(),
        [this](int a, int b) {
            // 找到各玩家手中最小的方块 rank
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
            return find_min_diamond(players[a].hand) < find_min_diamond(players[b].hand);
        });

    int first_pick = landlord_indices[0];
    int second_pick = landlord_indices[1];

    // 方块小的地主先选（先拿 pile_a）
    auto& hand_first = players[first_pick].hand;
    hand_first.insert(hand_first.end(), pile_a.begin(), pile_a.end());
    CardRule::SortHand(hand_first);

    auto& hand_second = players[second_pick].hand;
    hand_second.insert(hand_second.end(), pile_b.begin(), pile_b.end());
    CardRule::SortHand(hand_second);

    bottom_cards.clear();
}

// ===================================================================
// Room::HandlePlaying —— 出牌阶段
// ===================================================================
void Room::HandlePlaying(int fd, const std::string& json) {
    if (state != RoomState::PLAYING) return;

    int idx = GetPlayerIndex(fd);
    if (idx == -1) return;
    if (idx != current_turn) return;
    if (players[idx].IsHandEmpty()) return;

    // 解析 action 字段（简易字符串解析，避免引入 json 库）
    bool is_pass = (json.find("\"PASS\"") != std::string::npos ||
                    json.find("\"action\":\"PASS\"") != std::string::npos);
    bool is_play = (json.find("\"PLAY\"") != std::string::npos ||
                    json.find("\"action\":\"PLAY\"") != std::string::npos);

    if (is_pass) {
        // 新一轮自由出牌不允许 PASS
        if (last_player_idx == -1) return;

        pass_count++;
        if (pass_count >= 4) {
            // 新一轮开始
            current_turn = last_player_idx;
            last_player_idx = -1;
            last_played_cards.clear();
            pass_count = 0;
            BroadcastState();
            return;
        }
    } else if (is_play) {
        // 解析 cards 数组
        std::vector<uint8_t> play_cards;
        size_t pos = json.find("\"cards\"");
        if (pos == std::string::npos) return;
        pos = json.find('[', pos);
        if (pos == std::string::npos) return;
        size_t end = json.find(']', pos);
        if (end == std::string::npos) return;

        // 解析 [0,5,12] 格式
        std::string cards_str = json.substr(pos + 1, end - pos - 1);
        size_t start = 0;
        while (start < cards_str.size()) {
            // 跳过逗号和空格
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

        if (play_cards.empty()) return;

        // 验证手牌持有
        for (uint8_t c : play_cards) {
            if (std::find(players[idx].hand.begin(), players[idx].hand.end(), c) == players[idx].hand.end()) {
                return; // 出了自己没有的牌
            }
        }

        // 牌型判定
        CardType type = CardRule::EvaluateType(play_cards);
        if (type == CardType::INVALID) return;

        // 压制判定
        if (last_player_idx != -1) {
            if (!CardRule::CanBeat(play_cards, last_played_cards)) return;
        }

        // 检查炸弹，更新倍数
        if (type == CardType::BOMB) {
            // 3 张炸弹 ×2，4 张炸弹 ×4
            if (play_cards.size() == 3) {
                multiplier *= 2;
            } else if (play_cards.size() == 4) {
                multiplier *= 4;
            }
        } else if (type == CardType::ROCKET) {
            multiplier *= 4; // 王炸按 4 张炸弹处理
        }

        // 出牌成功
        players[idx].RemoveCards(play_cards);
        players[idx].has_played = true;
        last_played_cards = play_cards;
        last_player_idx = idx;
        pass_count = 0;
    } else {
        return; // 无法解析
    }

    // 检查胜负
    if (players[idx].IsHandEmpty()) {
        if (players[idx].is_landlord && CheckSpring()) {
            multiplier *= 2;  // 春天翻倍
        }
        state = RoomState::END;
        BroadcastState();
        return;
    }

    // 推进到下一个玩家
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
    // 手牌、is_landlord 等全部保留
}

// ===================================================================
// Room::HandleAIBidding —— AI 在叫地主阶段自动 PASS
// ===================================================================
void Room::HandleAIBidding(int player_idx) {
    if (state != RoomState::BIDDING) return;
    if (current_bidder_pos >= static_cast<int>(bidder_queue.size())) return;
    if (bidder_queue[current_bidder_pos] != player_idx) return;

    // AI 一律不叫地主
    players[player_idx].has_passed_bidding = true;
    current_bidder_pos++;

    if (landlord_count == 0 && current_bidder_pos >= static_cast<int>(bidder_queue.size())) {
        // 所有人都没叫（包括 AI）→ 重新发牌
        StartGame();
        return;
    }

    BroadcastState();
}

// ===================================================================
// Room::ExecuteAIDecision —— AI 出牌决策执行
// ===================================================================
void Room::ExecuteAIDecision(int player_idx, const std::string& action,
                              const std::vector<uint8_t>& cards) {
    if (state != RoomState::PLAYING) return;
    if (player_idx != current_turn) return;
    if (players[player_idx].IsHandEmpty()) return;

    if (action == "PASS") {
        // 新一轮自由出牌不允许 PASS
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

        // 防幻觉：验证手牌持有
        for (uint8_t c : cards) {
            if (std::find(players[player_idx].hand.begin(),
                          players[player_idx].hand.end(), c) == players[player_idx].hand.end()) {
                return; // AI 出了自己没有的牌，视为 PASS
            }
        }

        // 牌型判定
        CardType type = CardRule::EvaluateType(cards);
        if (type == CardType::INVALID) return;

        // 压制判定
        if (last_player_idx != -1) {
            if (!CardRule::CanBeat(cards, last_played_cards)) return;
        }

        // 炸弹翻倍
        if (type == CardType::BOMB) {
            if (cards.size() == 3) multiplier *= 2;
            else if (cards.size() == 4) multiplier *= 4;
        } else if (type == CardType::ROCKET) {
            multiplier *= 4;
        }

        players[player_idx].RemoveCards(cards);
        players[player_idx].has_played = true;
        last_played_cards = cards;
        last_player_idx = player_idx;
        pass_count = 0;
    } else {
        return;
    }

    // 检查胜负
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

    // state
    const char* state_str = "WAITING";
    switch (state) {
        case RoomState::BIDDING: state_str = "BIDDING"; break;
        case RoomState::PLAYING: state_str = "PLAYING"; break;
        case RoomState::END:     state_str = "END"; break;
        default: break;
    }
    ss << "\"state\":\"" << state_str << "\"";

    // my_cards
    ss << ",\"my_cards\":[";
    const auto& hand = players[player_idx].hand;
    for (size_t i = 0; i < hand.size(); ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(hand[i]);
    }
    ss << "]";

    // player_card_counts (每人剩余张数)
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
    bool first_landlord = true;
    for (int i = 0; i < 5; ++i) {
        if (players[i].is_landlord) {
            if (!first_landlord) ss << ",";
            ss << i;
            first_landlord = false;
        }
    }
    ss << "]";

    // last_played
    ss << ",\"last_played\":[";
    for (size_t i = 0; i < last_played_cards.size(); ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(last_played_cards[i]);
    }
    ss << "]";

    // last_player
    ss << ",\"last_player\":" << last_player_idx;

    // multiplier
    ss << ",\"multiplier\":" << multiplier;

    // reconnect_token (重连用)
    ss << ",\"reconnect_token\":\"" << players[player_idx].reconnect_token << "\"";

    // has_played (客户端判断春天态)
    ss << ",\"has_played\":" << (players[player_idx].has_played ? "true" : "false");

    // BIDDING 阶段额外字段
    if (state == RoomState::BIDDING) {
        ss << ",\"first_bidder\":" << (bidder_queue.empty() ? -1 : bidder_queue[0]);
        ss << ",\"second_bidder\":" << (bidder_queue.size() > 1 ? static_cast<int>(bidder_queue[1]) : -1);
        ss << ",\"current_bidder\":" << (current_bidder_pos < static_cast<int>(bidder_queue.size())
                                          ? bidder_queue[current_bidder_pos] : -1);
    }

    // PLAYING/END 阶段底牌可见
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
        // winner
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
        // 底分 × 倍数
        int64_t base = multiplier;  // multiplier 已含炸弹/春天翻倍
        ss << ",\"scores\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ss << ",";
            int64_t score = 0;
            if (landlord_win) {
                // 地主赢: 每个地主从每个农民赢 base
                score = players[i].is_landlord ? (3 * base) : (-2 * base);
            } else {
                // 农民赢: 每个农民从每个地主赢 base
                score = players[i].is_landlord ? (-3 * base) : (2 * base);
            }
            ss << score;
        }
        ss << "]";
    }

    ss << "}";
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
