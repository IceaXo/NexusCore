#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "Room.h"

// ===================================================================
// RoomManager —— 2000 房间内存对象池 + 玩家路由 + 断线 AI 托管
//
// 核心职责：
//   1. 对象池管理：std::vector<Room> 预分配 2000 个房间，O(1) 索引，无运行时堆分配
//   2. 玩家路由：fd → Room + 座位 快速查找，O(1) 均摊
//   3. 断线接管：玩家断开时将其座位 fd 标记为 -1（AI 托管）
//   4. 消息分发：OnMessage 解析 fd，定位 Room，调用对应状态机方法
//   5. 广播桥接：持有网络层连接表引用，为 Room 的 BroadcastState 提供发送通道
//
// 线程模型：单线程事件循环，RoomManager 的所有方法均在 epoll 线程内调用，
//          无需加锁（无锁沙盒原则）。
// ===================================================================
class RoomManager {
public:
    static constexpr int MAX_ROOMS = 2000;  // 对象池容量

    // ---- 玩家定位结构（内部用） ----
    struct PlayerLocation {
        int room_idx;    // rooms[room_idx] 即该玩家所在房间
        int player_idx;  // rooms[room_idx].players[player_idx] 即该玩家的座位
    };

    RoomManager();

    // ================================================================
    //  连接表注入（网络层 → 游戏层 的唯一耦合点）
    // ================================================================

    // EpollServer 在初始化时调用，把 connections 的地址注入。
    void SetConnectionMap(std::unordered_map<int, class Connection>* conn_map);

    // EpollServer 在启动 IPC 后调用，注入 IPC 客户端指针用于 AI 通信。
    void SetIPCClient(class IPCClient* ipc);

    // ================================================================
    //  玩家进出
    // ================================================================

    // 新玩家连接。遍历对象池找第一个 WAITING 状态的 Room，分配到空座位。
    // 若该 Room 凑齐 5 人，自动调用 room.StartGame() 开局。
    // 返回 true 表示分配成功，false 表示所有房间已满（万级并发下几乎不可能）。
    bool AddPlayer(int fd);

    // 玩家断线。查找 fd 对应的 Room 和座位，将该座位 fd 设为 -1（AI 托管）。
    // 若房间正在游戏中，AI 将在轮到该座位时接管出牌。
    // 若房间还在 WAITING/BIDDING 阶段，直接回收座位给新玩家。
    void RemovePlayer(int fd);

    // ================================================================
    //  消息入口（网络层调用的唯一入口）
    // ================================================================

    // 网络层切出完整 JSON 后调用此方法。
    // 内部：fd → 定位 Room → 根据 Room.state 分发到 HandleBidding/HandlePlaying。
    // 每次处理完后自动触发 AI 检测（若当前回合是 AI 座位，发起 IPC 请求）。
    void OnMessage(int fd, const std::string& json);

    // ================================================================
    //  AI 托管
    // ================================================================

    // EpollServer 在收到 IPC AI 决策后调用。
    void ApplyAIDecision(const std::string& json);

    // 房间内当前回合为 AI 时，发起 AI 决策请求。
    void RequestAIDecision(int room_idx, int player_idx);

    // OnMessage 收尾检查：若当前回合是 AI 座位，自动触发决策。
    void CheckAndTriggerAI(int room_idx);

    // 处理断线重连：在所有房间中搜索匹配 token
    bool HandleReconnect(int fd, const std::string& token);

    // ================================================================
    //  查询
    // ================================================================

    // 根据 fd 返回所在 Room 指针，用于广播等外部操作。
    // fd 不存在时返回 nullptr。
    Room* GetRoomByFd(int fd);

    // 当前已激活（非 WAITING）的房间数，用于监控
    int GetActiveRoomCount() const;

private:
    // ---- 2000 房间对象池 ----
    // std::vector 保证连续内存，Room 无虚函数，sizeof(Room) 紧凑
    std::vector<Room> rooms;

    // ---- fd → 房间/座位 快速查找 ----
    // key = Socket fd, value = {room_idx, player_idx}
    // std::unordered_map 底层是哈希表，O(1) 均摊查找
    // 玩家断线时从此表删除，Room.players[i].fd = -1 留在座位上
    std::unordered_map<int, PlayerLocation> fd_to_location;

    // ---- 网络层连接表引用 ----
    std::unordered_map<int, class Connection>* connections = nullptr;

    // ---- IPC 客户端引用 ----
    // 非拥有指针。EpollServer 持有真正的 IPCClient。
    // RoomManager 通过它向 Python AI 进程发送决策请求。
    class IPCClient* ipc_client = nullptr;

    // ---- 内部辅助 ----

    // 遍历 rooms，找 WAITING 或 END 房间（END 房间先 ResetRoom 回收）
    int FindWaitingRoom();

    // 在指定 Room 中找第一个 fd == -1 的空座位，找不到返回 -1
    static int FindEmptySeat(const Room& room);

    // 为指定 Room 构造 on_send 回调。
    // 回调做的事：connection.send_buffer.append(打包好的数据) → connection.WriteToSocket()
    // 这样 Room 不需要知道 Connection 类的细节。
    void BindSendCallback(Room& room);
};
