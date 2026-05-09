#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <chrono>
#include <random>    // std::mt19937
#include <functional> // std::function

// ===================================================================
// 房间状态机 —— 五人斗地主单局生命周期
// ===================================================================
enum class RoomState : uint8_t {
    WAITING,     // 等待玩家凑齐 5 人并全部 READY
    BIDDING,     // 叫地主阶段（按方块点数顺位依次表态）
    BOTTOM_PICK, // 底牌选择阶段（地主A盲选2张牌背）
    PLAYING,     // 出牌阶段（逆时针轮转）
    END,         // 本局结束（有人清空手牌）
};

// ===================================================================
// 单个玩家的牌桌上下文
// ===================================================================
struct PlayerContext {
    int fd = -1;                        // Socket 句柄，-1 代表该座位由 AI 托管
    std::vector<uint8_t> hand;          // 手牌数组
    bool is_landlord = false;
    bool has_passed_bidding = false;
    bool has_played = false;
    std::string reconnect_token;

    // ---- 身份字段 ----
    std::string name;                   // 玩家昵称（max 12 字符）
    int avatar = 0;                     // 头像编号 0-4
    bool is_ready = false;              // 准备状态（WAITING阶段）
    bool is_ai = false;                 // 是否为人机（ADD_BOT 添加或断线 AI 接管）
    bool is_autoplay = false;           // 是否开启了托管（真人挂机，AI 代打）

    bool IsHandEmpty() const { return hand.empty(); }

    void RemoveCards(const std::vector<uint8_t>& cards);
};

// ===================================================================
// Room —— 单局 5 人状态机沙盒
// ===================================================================
class Room {
public:
    // ---- 状态变量 ----
    RoomState state = RoomState::WAITING;

    PlayerContext players[5];
    int current_turn = 0;
    int last_player_idx = -1;
    int pass_count = 0;

    std::vector<uint8_t> last_played_cards;
    std::vector<uint8_t> player_last_played[5];
    std::vector<uint8_t> bottom_cards;

    int64_t multiplier = 1;

    // ---- 叫地主阶段专用变量 ----
    std::vector<int> bidder_queue;
    int current_bidder_pos = 0;
    int landlord_count = 0;

    // ---- 房间管理字段 ----
    int owner_seat = -1;                // 房主座位号，-1 表示无房主
    int total_rounds = 5;               // 总局数（房主可设定）
    int current_round = 0;              // 当前局数 (1..total_rounds)
    int64_t cumulative_scores[5] = {0}; // 每位玩家累计总分
    int64_t round_scores[5] = {0};      // 本局每位玩家得失分

    // ---- 底牌选择阶段 ----
    // 地主A从4张牌背中选中的索引 (0-3)，共选2个
    int bottom_pick_indices[2] = {-1, -1};
    int bottom_pick_count = 0;          // 已选数量 (0-2)
    int bottom_pick_landlord = -1;      // 正在选牌的地主A的座位号

    // ---- AI 延迟调度 ----
    int64_t ai_scheduled_at = 0;        // 计划的 AI 执行时间戳 (ms)，0 = 无待执行 AI
    int ai_delay_ms = 2000;             // AI 思考延迟 (ms)，2x 加速时为 1000

    // ---- 广播回调 ----
    using SendCallback = std::function<void(int fd, const std::string& json)>;
    SendCallback on_send;

    // ================================================================
    //  状态流转
    // ================================================================

    void StartGame();

    // 叫地主阶段
    void HandleBidding(int fd, const std::string& json);

    // 底牌选择阶段
    void HandlePickBottom(int fd, const std::string& json);

    // 出牌阶段
    void HandlePlaying(int fd, const std::string& json);

    // 提示
    void HandleHint(int fd);

    // AI 托管
    void SetAITakeover(int player_idx);

    // AI 决策执行
    void ExecuteAIDecision(int player_idx, const std::string& action,
                           const std::vector<uint8_t>& cards);

    // AI 叫地主阶段自动表态
    void HandleAIBidding(int player_idx);

    // AI 底牌选择阶段自动选择
    void HandleAIPickBottom(int player_idx);

    // 本局结束后重置房间（保留名字/头像/总分，fd=-1）
    void ResetRoom();

    // 从 END 回到 WAITING（保留 fd/名字/头像/总分，只清 ready 和游戏状态）
    void ReturnToWaiting();

    // 完全重置（清空名字/头像），用于回到 WAITING 重开
    void FullReset();

    int FindPlayerByToken(const std::string& token) const;
    void ReconnectPlayer(int player_idx, int new_fd);

    // 房主转移：房主离开时按座位顺序找下一位
    void TransferOwnership();

    // 检查座位是否被占用（真人或 AI）
    bool IsSeatOccupied(int seat) const;

    // 检查是否所有人 ready
    bool AllReady() const;

    // 序列化
    std::string SerializeState(int player_idx) const;
    void BroadcastState();

    // 序列化房间列表信息（给大厅玩家）
    std::string SerializeRoomInfo(int room_id) const;

private:
    int GetPlayerIndex(int fd) const;
    void AdvanceToNextPlayer();
    void DealCards(std::mt19937& rng);
    void BuildBidderQueue();
    void DistributeBottomCards();
    void DistributeBottomCardsAfterPick(const std::vector<uint8_t>& picked,
                                        const std::vector<uint8_t>& remaining);
    bool CheckSpring() const;
    std::string GenerateToken();

    // 获取地主A（方块较小者）的索引
    int GetFirstLandlord() const;
    // 获取地主B的索引
    int GetSecondLandlord() const;
};
