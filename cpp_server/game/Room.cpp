#include "Room.h"
#include "CardRule.h"
#include <algorithm>   // std::shuffle, std::find
#include <random>     // std::mt19937
#include <sstream>    // std::ostringstream (JSON 拼接用)

// ===================================================================
// PlayerContext::RemoveCards —— 从手牌中删除已打出的牌
// ===================================================================
void PlayerContext::RemoveCards(const std::vector<uint8_t>& cards) {
    // 1. 遍历 cards 中的每一张牌 c
    // 2. 在 hand 中用 std::find 找到 c 的位置
    // 3. 用 hand.erase(it) 删掉这张牌
    // 注意：cards 里的每张牌保证都在 hand 中（调用方已校验），所以 find 一定不会返回 end()
}

// ===================================================================
// Room::StartGame —— 凑齐 5 人后开局，洗牌发牌，状态切 BIDDING
// ===================================================================
void Room::StartGame() {
    // 1. 创建 std::mt19937 rng，用 std::random_device{}() 播种
    // 2. 调用 DealCards(rng) 洗牌发牌
    // 3. 遍历 players[0..4]，用 CardRule::SortHand 给每人的手牌排序
    // 4. 找方块3(编码0)的持有者 → first_bidder_idx
    //    找方块4(编码4)的持有者 → second_bidder_idx
    //    用 CardRule::GetSuit(c) == 0 判定方块花色，CardRule::GetRank(c) 判定点数
    // 5. 把 state 设为 RoomState::BIDDING
    // 6. 调用 BroadcastState() 通知所有人牌已发好
}

// ===================================================================
// Room::HandleBidding —— 叫地主阶段，处理 CALL / PASS
// ===================================================================
void Room::HandleBidding(int fd, const std::string& action) {
    // 1. 用 GetPlayerIndex(fd) 拿到座位号 idx，判断是否为 -1（无效fd）
    // 2. 判断当前轮到哪位候选人：
    //    bidder_turn == 0 → 当前轮到 first_bidder_idx
    //    bidder_turn == 1 → 当前轮到 second_bidder_idx
    // 3. 检查 fd 对应的玩家是否就是本轮该表态的候选人
    //    不是 → 忽略这条消息（不是你的回合）
    // 4. 如果 action == "CALL"：
    //    a. 该玩家 is_landlord = true
    //    b. landlord_count++
    //    c. has_passed_bidding = false
    // 5. 如果 action == "PASS"：
    //    a. 该玩家 has_passed_bidding = true
    // 6. bidder_turn++，推进到下一位候选人
    // 7. 判断叫地主阶段是否结束 (bidder_turn >= 2)：
    //    a. 如果 landlord_count == 0（两人都PASS）：
    //       - 重置叫地主状态（is_landlord/has_passed_bidding/bidder_turn 归零）
    //       - 所有玩家手牌清空，底牌清空
    //       - 回到 StartGame() 重新洗牌发牌
    //    b. 如果 landlord_count >= 1：
    //       - 调用 DistributeBottomCards() 分配底牌
    //       - state 切 PLAYING
    //       - current_turn = first_bidder_idx（方块3地主先手）
    //       - last_player_idx = -1（新一轮，自由出牌）
    //       - last_played_cards.clear()
    //       - pass_count = 0
    // 8. 调用 BroadcastState() 同步牌桌

    // TODO: 步骤 7a 的"两人都不叫" 是极其罕见的分支。
    //       如果你觉得逻辑复杂，可以先用 return 跳过，等 PLAYING 跑通再回来补。
}

// ===================================================================
// Room::HandlePlaying —— 出牌阶段，处理 PLAY / PASS
// ===================================================================
void Room::HandlePlaying(int fd, const std::string& json) {
    // 0. 状态守卫：如果 state != PLAYING，直接 return

    // 1. 用 GetPlayerIndex(fd) 拿到座位号 idx
    // 2. 检查 idx != current_turn → 不是你的回合，忽略
    // 3. 检查 players[idx].IsHandEmpty() → 手牌已空，忽略（理论上不会到这里）

    // 4. 解析 JSON：提取 "action" 字段
    //    提示：json 是字符串，暂时用 .find("\"action\":\"PLAY\"") 做简单判断
    //    后续可以换 json 库 (nlohmann/json)

    // ============================================================
    //  路径 A：PASS（不出）
    // ============================================================
    // A1. 如果 last_player_idx == -1（新一轮自由出牌）：PASS 不允许，忽略
    //     因为新一轮第一个出牌的人必须出牌，不能 pass
    // A2. pass_count++
    // A3. 检查是否 ≥4 人连续 pass（即 pass_count >= 4）：
    //     a. 新一轮开始：last_player_idx = -1, last_played_cards.clear(), pass_count = 0
    //     b. current_turn = last_player_idx（上一手出牌者获得新一轮出牌权）

    // ============================================================
    //  路径 B：PLAY（出牌）
    // ============================================================
    // B1. 从 JSON 中解析 cards 数组，填到 std::vector<uint8_t> play_cards
    // B2. 验证 play_cards 中的每张牌都在 players[idx].hand 中（不能出自己没有的牌）
    // B3. 调用 CardRule::EvaluateType(play_cards) 判断牌型
    //     如果返回 INVALID → 非法牌型，忽略
    // B4. 压制判定：
    //     a. 如果 last_player_idx == -1（新一轮）：不需要比较，自由出牌
    //     b. 否则：调用 CardRule::CanBeat(play_cards, last_played_cards)
    //        如果返回 false → 压不过，忽略
    // B5. 出牌成功：
    //     a. players[idx].RemoveCards(play_cards)
    //     b. last_played_cards = play_cards
    //     c. last_player_idx = idx
    //     d. pass_count = 0

    // ============================================================
    //  公共收尾（两个路径共用）
    // ============================================================
    // 5. 检查胜负：players[idx].IsHandEmpty()
    //    如果 true → state = END，BroadcastState()，return（游戏结束，不再轮转）
    // 6. AdvanceToNextPlayer() 推进到下一个有效玩家
    // 7. 判断新 current_turn 是否为 AI 座位（fd == -1）
    //    → 如果是，AI 决策暂时留空，等后面打通 IPC 再补
    // 8. 调用 BroadcastState() 同步牌桌

    // TODO: B1 的 JSON 解析目前用字符串查找临时替代，后续换 nlohmann/json
    // TODO: 步骤 7 的 AI 自动决策暂不实现
}

// ===================================================================
// Room::SetAITakeover —— 玩家断线，座位移交 AI
// ===================================================================
void Room::SetAITakeover(int player_idx) {
    // 1. players[player_idx].fd = -1
    // 2. 玩家原有的 is_landlord / 手牌等状态全部保留不变
    // 3. 如果 state == PLAYING 且 current_turn == player_idx：
    //    → AI 需要立刻为该座位做决策（当前版本跳过，等 IPC 打通）
}

// ===================================================================
// Room::SerializeState —— 为单个玩家生成牌桌快照 JSON
// ===================================================================
std::string Room::SerializeState(int player_idx) const {
    // 1. 用 std::ostringstream 拼接 JSON 字符串
    // 2. 必含字段：
    //    "state": "WAITING"/"BIDDING"/"PLAYING"/"END"
    //    "my_cards": [自己的手牌数组]
    //    "player_card_counts": [5个元素，每个是剩余张数]
    //    "current_turn": 当前轮到谁
    //    "is_landlord": [true/false]
    //    "landlords": [两个地主的座位号]
    //    "last_played": [桌面上要压的牌]
    //    "last_player": 上一手出牌者（-1 为新一轮）
    // 3. 关键规则：自己看到完整手牌，别人只看到张数
    //    遍历 5 个座位时：
    //    - i == player_idx → 输出 hand 数组
    //    - i != player_idx → 只输出 players[i].hand.size()
    // 4. BIDDING 阶段额外字段：
    //    "first_bidder": first_bidder_idx
    //    "second_bidder": second_bidder_idx
    //    "current_bidder": 当前该谁叫
    // 5. PLAYING 阶段额外字段：
    //    "bottom_cards": 底牌（已亮明，所有玩家可见）
    // 6. END 阶段额外字段：
    //    "winner": 获胜座位号

    return "{}"; // 占位，等你实现
}

// ===================================================================
// Room::BroadcastState —— 遍历 5 人，每人发一份属于他的快照
// ===================================================================
void Room::BroadcastState() {
    // 1. 如果 on_send 回调未设置（为 nullptr），直接 return
    // 2. 遍历 i = 0..4：
    //    a. 如果 players[i].fd != -1：
    //       - 调用 on_send(players[i].fd, SerializeState(i))
}

// ===================================================================
// Room::GetPlayerIndex —— 根据 fd 查座位号
// ===================================================================
int Room::GetPlayerIndex(int fd) const {
    // 1. 遍历 i = 0..4
    // 2. 如果 players[i].fd == fd → return i
    // 3. 没找到 → return -1
    return -1; // 占位
}

// ===================================================================
// Room::AdvanceToNextPlayer —— 推进 current_turn 到下一个有手牌的玩家
// ===================================================================
void Room::AdvanceToNextPlayer() {
    // 1. 死循环（最多转 5 次，必然找到一个有牌的或全员空牌）：
    //    a. current_turn = (current_turn + 1) % 5
    //    b. 如果 players[current_turn].hand 不为空 → break
    //    c. 如果转了一圈（回到了起始值）→ break（所有人手牌都空了）
}

// ===================================================================
// Room::DealCards —— 用梅森旋转算法洗牌发牌
// ===================================================================
void Room::DealCards(std::mt19937& rng) {
    // 1. 建立一副 54 张牌的数组 deck：
    //    普通牌：0..51（4花色 × 13点数）
    //    保留牌：52（跳过不用）
    //    小王：53
    //    保留：54, 55（跳过不用）
    //    大王：56
    //    实际可用牌：0..51, 53, 56 共 53 张
    //    （等等，一副标准牌 54 张 = 52 普通 + 2 Joker = 编码 53 和 56）
    //    deck 应该是所有 54 张：0..51 + 53 + 56
    //    用 for 循环 push_back 填满 deck

    // 2. 用 std::shuffle(deck.begin(), deck.end(), rng) 洗牌
    // 3. 每人发 10 张：
    //    遍历 i = 0..4:
    //       players[i].hand.clear()
    //       从 deck 中取 i*10 到 i*10+9 的 10 张，push 进 hand
    // 4. 剩余 4 张 = bottom_cards：
    //    bottom_cards.clear()
    //    取 deck[50] 到 deck[53] 的 4 张放入 bottom_cards
}

// ===================================================================
// Room::DistributeBottomCards —— 底牌分配给地主们
// ===================================================================
void Room::DistributeBottomCards() {
    // 1. 如果 landlord_count == 2（两个人都叫了地主）：
    //    a. 用 CardRule::GetSuit 找方块点数较小的地主先选
    //       比较 first_bidder_idx 和 second_bidder_idx 中
    //       谁持有方块的数字更小（方块3 < 方块4 < 方块5 ...）
    //    b. 从 bottom_cards 中取 2 张给第一个地主
    //       用 insert + SortHand 加入手牌
    //    c. 剩余 2 张给第二个地主，同样加入手牌
    // 2. 如果 landlord_count == 1（只有一人叫地主）：
    //    a. 找到 is_landlord == true 的玩家
    //    b. 全部 4 张底牌给他
    //    c. 加入手牌并排序
    // 3. bottom_cards.clear()
    // 4. 底牌已分完 → BroadcastState() 让所有人看到底牌内容
}
