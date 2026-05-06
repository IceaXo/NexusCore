#include "RoomManager.h"
#include "../network/Connection.h"
#include "CardRule.h"
#include <iostream>

// ===================================================================
// RoomManager::RoomManager —— 构造时预分配 2000 个房间
// ===================================================================
RoomManager::RoomManager() {
    // 1. rooms.reserve(MAX_ROOMS) —— 预分配内存，避免运行时扩容
    // 2. 用 for 循环 rooms.emplace_back() 填充 2000 个空房间
    //    Room 默认构造后 state = WAITING，直接可用
}

// ===================================================================
// RoomManager::SetConnectionMap —— 注入网络层连接表
// ===================================================================
void RoomManager::SetConnectionMap(std::unordered_map<int, class Connection>* conn_map) {
    // 1. connections = conn_map（保存裸指针）
    //    此方法的调用来自 EpollServer::Start() 之后、Loop() 之前
}

// ===================================================================
// RoomManager::AddPlayer —— 新玩家入座，凑齐 5 人自动开局
// ===================================================================
bool RoomManager::AddPlayer(int fd) {
    // 1. 调用 FindWaitingRoom() 找到一个有空座的 WAITING 房间
    // 2. 如果返回 -1（所有房间都满了）：
    //    打印 "房间已满" 日志，return false
    // 3. 调用 FindEmptySeat(rooms[room_idx]) 找到该房间的空座位
    // 4. 占座：
    //    a. rooms[room_idx].players[seat].fd = fd
    //    b. rooms[room_idx].players[seat].hand.clear()
    //    c. rooms[room_idx].players[seat].is_landlord = false
    //    d. rooms[room_idx].players[seat].has_passed_bidding = false
    // 5. 更新 fd_to_location[fd] = {room_idx, seat}
    // 6. 调用 BindSendCallback(rooms[room_idx]) 为该房间绑定广播回调
    //    （每次 AddPlayer 都调一次是幂等的，只在第一次有效）
    // 7. 检查该房间是否已满 5 人：
    //    遍历 rooms[room_idx].players[0..4]，统计 fd != -1 的人数
    //    如果 == 5 → rooms[room_idx].StartGame()
    // 8. return true

    return true; // 占位
}

// ===================================================================
// RoomManager::RemovePlayer —— 玩家断线，座位交给 AI 或回收
// ===================================================================
void RoomManager::RemovePlayer(int fd) {
    // 1. 在 fd_to_location 中查找 fd → PlayerLocation
    //    如果找不到（fd 不在任何房间），打印警告并 return
    // 2. 取出 room_idx 和 player_idx
    // 3. 从 fd_to_location 中 erase(fd)
    // 4. 根据 rooms[room_idx].state 分流处理：
    //
    //    a. state == WAITING：
    //       - 该座位置空：players[player_idx].fd = -1
    //       - 其他不变，新玩家连入时会重新占用
    //
    //    b. state == BIDDING：
    //       - 如果断线的是叫地主候选人之一：
    //         该候选人默认 PASS，调用 rooms[room_idx].HandleBidding(fd, "PASS")
    //         （HandleBidding 里用 fd 找不到座位，需要改为直接用 player_idx 调用）
    //       - 否则：players[player_idx].fd = -1，位置由 AI 接管
    //
    //    c. state == PLAYING：
    //       - 如果当前回合轮到该玩家（current_turn == player_idx）：
    //         → 等 IPC 打通后，AI 会自动决策。当前版本先跳过该回合
    //       - players[player_idx].fd = -1（AI 接管座位）

    // TODO: 当前 BIDDING/PLAYING 断线逻辑是简化版，完整版需要 IPC 介入
}

// ===================================================================
// RoomManager::OnMessage —— 网络层消息入口，按状态机分发
// ===================================================================
void RoomManager::OnMessage(int fd, const std::string& json) {
    // 1. 在 fd_to_location 中查找 fd → PlayerLocation
    //    如果找不到，打印 "未入座玩家发来消息" 并 return
    // 2. Room& room = rooms[loc.room_idx]
    // 3. 根据 room.state 分发：
    //    switch (room.state) {
    //        case WAITING:  // 房间还未满员，忽略一切消息
    //            break;
    //        case BIDDING:
    //            从 json 提取 action ("CALL" 或 "PASS")
    //            room.HandleBidding(fd, action);
    //            break;
    //        case PLAYING:
    //            room.HandlePlaying(fd, json);
    //            break;
    //        case END:
    //            // 游戏已结束，忽略一切操作
    //            break;
    //    }
    // 4. 消息处理后自动调用 room.BroadcastState()
    //    先判断 room.state != WAITING（WAITING 状态下没有游戏状态可广播）
    //    注意：HandleBidding/HandlePlaying 内部已经调了 BroadcastState，
    //         这里再调一次会导致重复广播。
    //         解决方案：只在 OnMessage 收尾调一次 BroadcastState，
    //         让 Handle* 内部不调，收敛广播入口。
}

// ===================================================================
// RoomManager::GetRoomByFd —— 根据 fd 查所在房间
// ===================================================================
Room* RoomManager::GetRoomByFd(int fd) {
    // 1. 在 fd_to_location 中查找 fd
    // 2. 找到 → return &rooms[it->second.room_idx]
    // 3. 没找到 → return nullptr
    return nullptr; // 占位
}

// ===================================================================
// RoomManager::GetActiveRoomCount —— 活跃房间数（用于监控/压测）
// ===================================================================
int RoomManager::GetActiveRoomCount() const {
    // 1. int count = 0
    // 2. 遍历 rooms：
    //    如果 room.state != WAITING → count++
    // 3. return count
    return 0; // 占位
}

// ===================================================================
// RoomManager::FindWaitingRoom —— 找第一个有空座且状态为 WAITING 的房间
// ===================================================================
int RoomManager::FindWaitingRoom() const {
    // 1. 遍历 rooms[0..MAX_ROOMS-1]
    // 2. 如果 room.state == WAITING：
    //    遍历 room.players[0..4]，如果存在 fd == -1 → return 该房间索引
    // 3. 全部找完都没有 → return -1
    return -1; // 占位
}

// ===================================================================
// RoomManager::FindEmptySeat —— 在指定房间中找第一个空座位
// ===================================================================
int RoomManager::FindEmptySeat(const Room& room) {
    // 1. 遍历 i = 0..4
    // 2. 如果 room.players[i].fd == -1 → return i
    // 3. return -1（没有空座）
    return -1; // 占位
}

// ===================================================================
// RoomManager::BindSendCallback —— 为房间绑定网络发送回调
// ===================================================================
void RoomManager::BindSendCallback(Room& room) {
    // 1. 如果 connections == nullptr → return（还没注入连接表）
    // 2. room.on_send = [this](int fd, const std::string& json) {
    //        // 在 connections 中查找 fd
    //        auto it = connections->find(fd);
    //        if (it == connections->end()) return;
    //
    //        // 打包成 [4字节大端长度头][JSON]
    //        uint32_t net_len = htonl(json.size());
    //        std::string packet(4, '\0');
    //        std::memcpy(&packet[0], &net_len, 4);
    //        packet.append(json);
    //
    //        // 追加到 send_buffer，尝试 write
    //        it->second.send_buffer.append(packet);
    //        it->second.WriteToSocket();
    //    };
    //
    // 注意：lambda 按值捕获 [this]，因为 Room 的生命周期在 RoomManager 内，
    //       this 指针在整个 main() 期间都有效。
    //       json 和 fd 按值传递 (已在函数签名中)，lambda 内直接使用。
}
