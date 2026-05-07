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
    BIDDING,  // 叫地主阶段（按方块点数顺位依次表态）
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
    bool has_played = false;            // 是否出过牌（用于春天判定）
    std::string reconnect_token;        // 断线重连令牌（入座时分配）

    bool IsHandEmpty() const { return hand.empty(); }

    void RemoveCards(const std::vector<uint8_t>& cards);
};

// ===================================================================
// Room —— 单局 5 人状态机沙盒
//
// 阵营：2 地主（盟友） vs 3 农民（盟友）
//
// 叫地主规则：
//   1. 发牌后按方块点数从小到大（方块3→方块4→方块5→...→方块A→方块2）
//      找到各持有者，去重后形成 bidder_queue
//   2. 按队列顺序依次表态 CALL/PASS。同一方块点数的持有者跳过（已在队列中）
//   3. 若方块3和方块4在同一人手中，则顺位到方块5的持有者，以此类推
//   4. 凑齐 2 人 CALL → 进入出牌阶段
//   5. 若有人 PASS → 顺位到下一个方块点数的持有者
//   6. 若全部 PASS → 重新洗牌发牌
//
// 出牌流转：
//   - 持有更小方块的地主先出牌
//   - 逆时针轮转：current_turn = (current_turn + 1) % 5
//   - 4 人连续 pass 后，上一手出牌者开始新一轮（可出任意牌型）
//   - 任意玩家清空手牌 → 游戏立刻冻结，状态切 END
//
// 计分：
//   - 底分 1，过程中炸弹/春天事件翻倍
//   - 3 张炸弹 ×2，4 张炸弹 ×4，春天 ×2
// ===================================================================
class Room {
public:
    // ---- 状态变量 ----
    RoomState state = RoomState::WAITING;

    PlayerContext players[5];        // 5 个座位的玩家上下文
    int current_turn = 0;           // 当前轮到谁出牌 (0-4)
    int last_player_idx = -1;       // 上一手有效出牌的玩家索引（-1 表示新一轮）
    int pass_count = 0;             // 连续 pass 次数，>=4 时进入新一轮

    std::vector<uint8_t> last_played_cards; // 桌面上要压制的牌（新一轮时为 empty）
    std::vector<uint8_t> player_last_played[5]; // 每个玩家最近一次出的牌（客户端头像旁展示）
    std::vector<uint8_t> bottom_cards;      // 底牌 4 张

    int64_t multiplier = 1;         // 倍数（底分 × 倍数 = 最终分）

    // ---- 叫地主阶段专用变量 ----
    // 按方块点数排序的候选人队列，如 [2, 0, 4, 1, 3]
    // 表示方块3在玩家2手中，方块4在玩家0手中，方块5在玩家4手中...
    std::vector<int> bidder_queue;
    int current_bidder_pos = 0;     // 当前正在等待表态的队列位置
    int landlord_count = 0;         // 已确认的地主人数 (0/1/2)

    // ---- 广播回调 ----
    // Room 不直接持有网络层引用，而是通过回调发送消息。
    // 函数签名：void(int fd, const std::string& json)
    using SendCallback = std::function<void(int fd, const std::string& json)>;
    SendCallback on_send;

    // ================================================================
    //  状态流转
    // ================================================================

    // 凑齐 5 人后触发。洗牌发牌，构建 bidder_queue，状态切 BIDDING。
    void StartGame();

    // 叫地主阶段。fd 发来 "CALL" 或 "PASS"。
    // 按 bidder_queue 顺序依次表态，动态顺位直到凑齐 2 个地主或全员 PASS。
    void HandleBidding(int fd, const std::string& action);

    // 出牌阶段。解析 fd 发来的 JSON：
    //   {"action":"PLAY","cards":[0,5,12]}  或  {"action":"PASS"}
    void HandlePlaying(int fd, const std::string& json);

    // 提示：返回当前手牌所有能压过 last_cards 的合法出牌组合
    void HandleHint(int fd);

    // 玩家断线，该座位移交 AI 托管。
    void SetAITakeover(int player_idx);

    // AI 决策执行入口 —— 由 RoomManager 在收到 IPC 回包后调用。
    // action: "PLAY" 或 "PASS"
    // cards:  要出的牌（PASS 时为空）
    void ExecuteAIDecision(int player_idx, const std::string& action,
                           const std::vector<uint8_t>& cards);

    // AI 在叫地主阶段自动表态（一律 PASS）
    void HandleAIBidding(int player_idx);

    // 本局结束后重置房间，回到 WAITING 供下一局复用
    void ResetRoom();

    // 用给定 token 查找玩家座位，找不到返回 -1
    int FindPlayerByToken(const std::string& token) const;

    // 断线重连：该座位恢复 fd
    void ReconnectPlayer(int player_idx, int new_fd);

    // ================================================================
    //  序列化
    // ================================================================

    std::string SerializeState(int player_idx) const;
    void BroadcastState();

private:
    int GetPlayerIndex(int fd) const;
    void AdvanceToNextPlayer();
    void DealCards(std::mt19937& rng);
    void BuildBidderQueue();       // 按方块点数构建叫地主候选人队列
    void DistributeBottomCards();
    bool CheckSpring() const;      // 春天判定
    std::string GenerateToken();   // 生成随机重连令牌
};
