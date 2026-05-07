#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <random>    // std::mt19937 —— 梅森旋转洗牌算法
#include <functional> // std::function —— 广播回调

// ===================================================================
// 房间状态机 —— 五人斗地主单局生命周期
// ===================================================================
enum class RoomState : uint8_t {
    WAITING,  // 等待玩家凑齐 5 人
    BIDDING,  // 叫地主阶段（持有方块3/方块4 的玩家依次表态）
    PLAYING,  // 出牌阶段（逆时针轮转）
    END,      // 本局结束（有人清空手牌）
};

// ===================================================================
// 单个玩家的牌桌上下文
// ===================================================================
struct PlayerContext {
    int fd = -1;                        // Socket 句柄，-1 代表该座位由 AI 托管
    std::vector<uint8_t> hand;          // 手牌数组，如 [0, 5, 12, 37, 53]
    bool is_landlord = false;           // 是否已确认为地主
    bool has_passed_bidding = false;    // 叫地主阶段是否已表态"不叫"

    // 手牌是否已清空（该玩家胜出）
    bool IsHandEmpty() const { return hand.empty(); }

    // 从 hand 中移除指定卡牌。调用前应确保 cards 中的每张牌都在 hand 中。
    // erase-remove 惯用法：remove_if 把要删的元素甩到末尾，erase 一刀切掉。
    void RemoveCards(const std::vector<uint8_t>& cards);
};

// ===================================================================
// Room —— 单局 5 人状态机沙盒
//
// 阵营：2 地主（盟友） vs 3 农民（盟友）
//
// 叫地主规则：
//   1. 发牌后找到持有 方块3(编码0) 和 方块4(编码4) 的两名玩家
//   2. 方块3 持有者先叫，方块4 持有者后叫
//   3. 若两人均叫地主 → 底牌 4 张平分 (每人 2 张)，亮明收入手牌
//   4. 若仅一人叫地主 → 独享全部 4 张底牌
//   5. 若两人均不叫 → 重新洗牌发牌
//
// 出牌流转：
//   - 地主先出牌（方块3 持有地主先手）
//   - 逆时针轮转：current_turn = (current_turn + 1) % 5
//   - 4 人连续 pass 后，上一手出牌者开始新一轮（可出任意牌型）
//   - 任意玩家清空手牌 → 游戏立刻冻结，状态切 END
//
// 出牌规则：
//   - 合法牌型：单张/对子/顺子(>=3)/连对(>=2)/三带一/三带二/四带二/炸弹/王炸
//   - 王炸 > 炸弹(4张) > 炸弹(3张) > 普通牌型
//   - 同类型必须同张数，最大点数更大才能压
// ===================================================================
class Room {
public:
    // ---- 状态变量 ----
    RoomState state = RoomState::WAITING;

    PlayerContext players[5];        // 5 个座位的玩家上下文
    int current_turn = 0;           // 当前轮到谁出牌 (0-4)
    int last_player_idx = -1;       // 上一手有效出牌的玩家索引（-1 表示新一轮，无牌要压）
    int pass_count = 0;             // 连续 pass 次数，>=4 时进入新一轮

    std::vector<uint8_t> last_played_cards; // 桌面上要压制的牌（新一轮时为 empty）
    std::vector<uint8_t> bottom_cards;      // 底牌 4 张

    // ---- 叫地主阶段专用变量 ----
    int first_bidder_idx  = -1;     // 方块3 持有者的座位号 (0-4)
    int second_bidder_idx = -1;     // 方块4 持有者的座位号 (0-4)
    int landlord_count = 0;         // 已确认的地主人数 (0/1/2)
    int bidder_turn = 0;            // 当前轮到第几个候选地主叫牌 (0=第一位, 1=第二位)

    // ---- 广播回调 ----
    // Room 不直接持有网络层引用，而是通过回调发送消息。
    // 函数签名：void(int fd, const std::string& json)
    // 由 RoomManager::OnMessage 在调用 Room 方法前注入。
    using SendCallback = std::function<void(int fd, const std::string& json)>;
    SendCallback on_send;           // 发牌桌快照给单个玩家的回调

    // ================================================================
    //  状态流转
    // ================================================================

    // 凑齐 5 人后触发。用 std::mt19937 洗牌 0-51 + 53 + 56，每人发 10 张，
    // 留 4 张作底牌。找到方块3/方块4 持有者，状态切 BIDDING。
    // std::mt19937 是 C++11 标准库梅森旋转伪随机数生成器，比 rand() 质量高得多。
    void StartGame();

    // 叫地主阶段。fd 发来 "CALL" 或 "PASS"。
    // 两位候选人依次表态后：分配底牌，状态切 PLAYING，地主先手出牌。
    void HandleBidding(int fd, const std::string& action);

    // 出牌阶段。解析 fd 发来的 JSON：
    //   {"action":"PLAY","cards":[0,5,12]}  或  {"action":"PASS"}
    // 出牌路径：验证手牌持有 → CardRule::EvaluateType → CardRule::CanBeat
    //          → 从 hand 移除 → 更新桌面状态 → 检查胜负 → 轮转
    // Pass路径：pass_count++ → 检查是否新一轮 → 轮转
    void HandlePlaying(int fd, const std::string& json);

    // 玩家断线，该座位移交 AI 托管。fd 标记为 -1，is_landlord 等状态不变。
    // 轮到该座位时，由 Python AI 进程代为决策。
    void SetAITakeover(int player_idx);

    // ================================================================
    //  序列化
    // ================================================================

    // 为指定座位玩家生成牌桌快照 JSON。
    // 包含：自己的手牌、各玩家剩余张数、桌面 last_played、当前轮到谁、
    //       自己的身份（地主/农民）、底牌（开局后亮明）。
    // 不同座位看到的 JSON 不同：自己的手牌可见，别人的手牌仅显示张数。
    std::string SerializeState(int player_idx) const;

    // 遍历 5 个座位，对每个 fd != -1 的玩家调用 on_send(fd, SerializeState(i))
    void BroadcastState();

private:
    // ---- 内部辅助 ----

    // 根据 fd 查找座位号 0-4，未找到返回 -1
    int GetPlayerIndex(int fd) const;

    // 跳过手牌已空的玩家，将 current_turn 推进到下一个有效座位
    void AdvanceToNextPlayer();

    // 发牌：std::mt19937 洗牌，每人 10 张，底牌 4 张，各自排序
    void DealCards(std::mt19937& rng);

    // 底牌分给地主们，加入手牌后重新排序
    void DistributeBottomCards();
};
