#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "Room.h"

// ===================================================================
// RoomManager —— 5 房间对象池 + 大厅 + 玩家路由 + 断线 AI 托管
// ===================================================================
class RoomManager {
public:
    static constexpr int MAX_ROOMS = 5;  // 房间池容量

    struct PlayerLocation {
        int room_idx;
        int player_idx;
    };

    // 大厅玩家信息
    struct LobbyPlayer {
        std::string name;
        int avatar = 0;
    };

    RoomManager();

    // ---- 连接表 / IPC 注入 ----
    void SetConnectionMap(std::unordered_map<int, class Connection>* conn_map);
    void SetIPCClient(class IPCClient* ipc);

    // ================================================================
    //  玩家进出
    // ================================================================

    // 新玩家连接（进入大厅，不分配房间）
    void AddToLobby(int fd);

    // 玩家加入指定房间
    bool JoinRoom(int fd, int room_id);

    // 玩家离开房间回到大厅
    void LeaveRoom(int fd);

    // 玩家断线
    void RemovePlayer(int fd);

    // ================================================================
    //  消息入口
    // ================================================================

    void OnMessage(int fd, const std::string& json);

    // ================================================================
    //  AI 托管
    // ================================================================

    void ApplyAIDecision(const std::string& json);
    void RequestAIDecision(int room_idx, int player_idx);
    void CheckAndTriggerAI(int room_idx);
    void ProcessScheduledAI();
    bool HandleReconnect(int fd, const std::string& token);

    // ================================================================
    //  查询
    // ================================================================

    Room* GetRoomByFd(int fd);
    int GetActiveRoomCount() const;

    // 判断玩家是否在大厅
    bool IsInLobby(int fd) const;

    // 广播房间列表给所有大厅玩家
    void BroadcastRoomList();

private:
    std::vector<Room> rooms;

    // fd → 房间/座位
    std::unordered_map<int, PlayerLocation> fd_to_location;

    // 大厅玩家：fd → {name, avatar}
    std::unordered_map<int, LobbyPlayer> lobby_players;

    std::unordered_map<int, class Connection>* connections = nullptr;
    class IPCClient* ipc_client = nullptr;

    // ---- 内部辅助 ----

    int FindWaitingRoom();
    static int FindEmptySeat(const Room& room);
    void BindSendCallback(Room& room);

    // 大厅消息处理
    void HandleSetName(int fd, const std::string& json);

    // 房间内消息处理
    void HandleReady(int fd);
    void HandleSetRounds(int fd, const std::string& json);
    void HandleContinue(int fd);
    void HandleAddBot(int room_idx);
    void HandleRemoveBot(int room_idx, int seat);
};
