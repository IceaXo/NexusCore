#include "RoomManager.h"
#include "../network/Connection.h"
#include "../ipc/IPCClient.h"
#include "CardRule.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>

static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ===================================================================
// RoomManager::RoomManager
// ===================================================================
RoomManager::RoomManager() {
    rooms.reserve(MAX_ROOMS);
    for (int i = 0; i < MAX_ROOMS; ++i) {
        rooms.emplace_back();
    }
}

// ===================================================================
// RoomManager::SetConnectionMap / SetIPCClient
// ===================================================================
void RoomManager::SetConnectionMap(std::unordered_map<int, class Connection>* conn_map) {
    connections = conn_map;
}

void RoomManager::SetIPCClient(class IPCClient* ipc) {
    ipc_client = ipc;
}

// ===================================================================
// RoomManager::BindSendCallback
// ===================================================================
void RoomManager::BindSendCallback(Room& room) {
    if (!connections) return;

    room.on_send = [this](int fd, const std::string& json) {
        auto it = connections->find(fd);
        if (it == connections->end()) return;

        uint32_t net_len = htonl(static_cast<uint32_t>(json.size()));
        std::string packet(4, '\0');
        std::memcpy(&packet[0], &net_len, 4);
        packet.append(json);

        it->second.send_buffer.append(packet);
        it->second.WriteToSocket();
    };
}

// ===================================================================
// 向单个玩家发送 JSON（大厅用）
// ===================================================================
static void SendToPlayer(std::unordered_map<int, Connection>* connections,
                         int fd, const std::string& json) {
    if (!connections) return;
    auto it = connections->find(fd);
    if (it == connections->end()) return;

    uint32_t net_len = htonl(static_cast<uint32_t>(json.size()));
    std::string packet(4, '\0');
    std::memcpy(&packet[0], &net_len, 4);
    packet.append(json);

    it->second.send_buffer.append(packet);
    it->second.WriteToSocket();
}

// ===================================================================
// RoomManager::FindWaitingRoom
// ===================================================================
int RoomManager::FindWaitingRoom() {
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (rooms[i].state == RoomState::WAITING) {
            for (int j = 0; j < 5; ++j) {
                if (!rooms[i].IsSeatOccupied(j)) return i;
            }
        }
    }
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (rooms[i].state == RoomState::END) {
            rooms[i].ResetRoom();
            return i;
        }
    }
    return -1;
}

// ===================================================================
// RoomManager::FindEmptySeat
// ===================================================================
int RoomManager::FindEmptySeat(const Room& room) {
    for (int i = 0; i < 5; ++i) {
        if (!room.IsSeatOccupied(i)) return i;
    }
    return -1;
}

// ===================================================================
// RoomManager::IsInLobby
// ===================================================================
bool RoomManager::IsInLobby(int fd) const {
    return lobby_players.find(fd) != lobby_players.end();
}

// ===================================================================
// RoomManager::AddToLobby —— 新连接进入大厅
// ===================================================================
void RoomManager::AddToLobby(int fd) {
    // 清理旧状态
    auto existing = fd_to_location.find(fd);
    if (existing != fd_to_location.end()) {
        RemovePlayer(fd);
    }
    lobby_players[fd] = {"", 0};

    // 发送房间列表
    std::ostringstream ss;
    ss << "{\"type\":\"room_list\",\"rooms\":[";
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (i > 0) ss << ",";
        ss << rooms[i].SerializeRoomInfo(i + 1); // room_id 从 1 开始
    }
    ss << "]}";
    SendToPlayer(connections, fd, ss.str());
}

// ===================================================================
// RoomManager::BroadcastRoomList —— 广播给所有大厅玩家
// ===================================================================
void RoomManager::BroadcastRoomList() {
    std::ostringstream ss;
    ss << "{\"type\":\"room_list\",\"rooms\":[";
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (i > 0) ss << ",";
        ss << rooms[i].SerializeRoomInfo(i + 1);
    }
    ss << "]}";

    std::string json = ss.str();
    for (const auto& kv : lobby_players) {
        SendToPlayer(connections, kv.first, json);
    }
}

// ===================================================================
// RoomManager::JoinRoom
// ===================================================================
bool RoomManager::JoinRoom(int fd, int room_id) {
    // room_id 1-5 映射到 rooms[0-4]
    int room_idx = room_id - 1;
    if (room_idx < 0 || room_idx >= MAX_ROOMS) return false;

    Room& room = rooms[room_idx];

    // 如果所有真人已离开但房间还在游戏状态（人机残局），重置房间
    bool all_disconnected = true;
    for (int i = 0; i < 5; ++i) {
        if (room.players[i].fd != -1) { all_disconnected = false; break; }
    }
    if (all_disconnected && room.state != RoomState::WAITING && room.state != RoomState::END) {
        room.FullReset();
    }

    // 只允许加入 WAITING 或 END 状态的房间
    if (room.state != RoomState::WAITING && room.state != RoomState::END) return false;
    if (room.state == RoomState::END) {
        room.ResetRoom();
    }

    // 查找大厅中该 fd 对应的玩家信息
    auto lobby_it = lobby_players.find(fd);
    std::string lobby_name = (lobby_it != lobby_players.end()) ? lobby_it->second.name : "";
    int lobby_avatar = (lobby_it != lobby_players.end()) ? lobby_it->second.avatar : 0;

    // 先检查是否存在断线重连座位（神魂匹配）
    int seat = -1;
    for (int i = 0; i < 5; ++i) {
        if (room.players[i].fd == -1 && !room.players[i].is_ai
            && !lobby_name.empty() && room.players[i].name == lobby_name) {
            seat = i;
            break;
        }
    }

    // 未匹配到断线座位 → 正常找空位
    if (seat == -1) {
        seat = FindEmptySeat(room);
    }
    if (seat == -1) return false; // 满员

    // 从大厅移除并接管座位
    if (lobby_it != lobby_players.end()) {
        PlayerContext& player = room.players[seat];
        player.fd = fd;
        player.name = lobby_it->second.name;
        player.avatar = lobby_it->second.avatar;
        player.is_ready = false;
        lobby_players.erase(lobby_it);
    } else {
        // 不在大厅（重连等场景），直接分配
        room.players[seat].fd = fd;
        room.players[seat].is_ready = false;
    }

    // 第一个进入的玩家成为房主
    if (room.owner_seat == -1 || !room.IsSeatOccupied(room.owner_seat)) {
        room.owner_seat = seat;
    }

    fd_to_location[fd] = {room_idx, seat};
    BindSendCallback(room);

    // 推送房间状态给该玩家
    if (room.on_send) {
        room.on_send(fd, room.SerializeState(seat));
    }

    // 广播房间状态给房间内其他人
    room.BroadcastState();

    // 更新房间列表给大厅玩家
    BroadcastRoomList();

    // 检查是否满 5 人且全部 ready → 开局
    int player_count = 0;
    for (int i = 0; i < 5; ++i) {
        if (room.IsSeatOccupied(i)) player_count++;
    }
    if (player_count == 5 && room.AllReady()) {
        room.StartGame();
    }

    return true;
}

// ===================================================================
// RoomManager::LeaveRoom
// ===================================================================
void RoomManager::LeaveRoom(int fd) {
    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    int room_idx = it->second.room_idx;
    int player_idx = it->second.player_idx;
    Room& room = rooms[room_idx];

    // 保存名字和头像（回大厅用）
    std::string name = room.players[player_idx].name;
    int avatar = room.players[player_idx].avatar;

    // 清空座位（完全重置）
    room.players[player_idx] = PlayerContext{};

    fd_to_location.erase(it);

    // 房主转移
    if (room.owner_seat == player_idx) {
        room.TransferOwnership();
    }

    // 有真人玩家离开 → 重置全员分数和局数，下一局从头开始
    for (int i = 0; i < 5; ++i) {
        room.cumulative_scores[i] = 0;
        room.round_scores[i] = 0;
    }
    room.current_round = 0;

    // 如果没有真人玩家了，清空房间（包括人机）
    bool has_real_player = false;
    for (int i = 0; i < 5; ++i) {
        if (room.players[i].fd != -1) { has_real_player = true; break; }
    }
    if (!has_real_player) {
        room.FullReset();
    }

    // 玩家回到大厅
    lobby_players[fd] = {name, avatar};

    // 广播房间状态
    room.BroadcastState();
    BroadcastRoomList();
}

// ===================================================================
// RoomManager::RemovePlayer —— 断线处理
// ===================================================================
void RoomManager::RemovePlayer(int fd) {
    // 大厅玩家断线
    auto lobby_it = lobby_players.find(fd);
    if (lobby_it != lobby_players.end()) {
        lobby_players.erase(lobby_it);
        return;
    }

    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    int room_idx = it->second.room_idx;
    int player_idx = it->second.player_idx;
    fd_to_location.erase(it);

    Room& room = rooms[room_idx];
    bool was_owner = (room.owner_seat == player_idx);

    switch (room.state) {
        case RoomState::WAITING: {
            // WAITING 状态断线：AI 接管，自动 is_ready = true
            room.players[player_idx].fd = -1;
            room.players[player_idx].is_ready = true;

            // 房主转移
            if (was_owner) room.TransferOwnership();

            // 检查是否满足开局条件（5人全 ready）
            if (room.AllReady()) {
                room.StartGame();
            } else {
                room.BroadcastState();
                BroadcastRoomList();
            }
            break;
        }
        case RoomState::BIDDING: {
            if (static_cast<int>(room.bidder_queue.size()) > room.current_bidder_pos &&
                room.bidder_queue[room.current_bidder_pos] == player_idx) {
                room.players[player_idx].fd = -1;
                room.HandleAIBidding(player_idx);
            } else {
                room.players[player_idx].fd = -1;
            }
            break;
        }
        case RoomState::BOTTOM_PICK: {
            if (player_idx == room.bottom_pick_landlord) {
                // 正在选牌的地主A断线 → AI 随机选
                room.players[player_idx].fd = -1;
                room.HandleAIPickBottom(player_idx);
            } else {
                // 地主B 或农民 → 标记断线
                room.players[player_idx].fd = -1;
                room.BroadcastState();
            }
            break;
        }
        case RoomState::PLAYING: {
            room.SetAITakeover(player_idx);
            room.BroadcastState();
            break;
        }
        case RoomState::END: {
            room.players[player_idx].fd = -1;
            room.BroadcastState();
            break;
        }
    }

    // 断线后检查 AI
    if (room.state == RoomState::PLAYING || room.state == RoomState::BIDDING ||
        room.state == RoomState::BOTTOM_PICK) {
        CheckAndTriggerAI(room_idx);
    }
}

// ===================================================================
// RoomManager::HandleSetName —— 设置名字和头像
// ===================================================================
void RoomManager::HandleSetName(int fd, const std::string& json) {
    // 提取 name
    std::string name;
    size_t pos = json.find("\"name\"");
    if (pos != std::string::npos) {
        pos = json.find('"', pos + 6);
        if (pos != std::string::npos) {
            size_t end = json.find('"', pos + 1);
            if (end != std::string::npos) {
                name = json.substr(pos + 1, end - pos - 1);
            }
        }
    }
    if (name.size() > 12) name = name.substr(0, 12);

    // 提取 avatar
    int avatar = 0;
    pos = json.find("\"avatar\"");
    if (pos != std::string::npos) {
        pos = json.find(':', pos);
        if (pos != std::string::npos) {
            avatar = std::stoi(json.substr(pos + 1));
        }
    }
    if (avatar < 0 || avatar > 4) avatar = 0;

    // 检查是否在房间内
    auto it = fd_to_location.find(fd);
    if (it != fd_to_location.end()) {
        Room& room = rooms[it->second.room_idx];
        int idx = it->second.player_idx;
        room.players[idx].name = name;
        room.players[idx].avatar = avatar;

        // 广播更新后的房间状态
        room.BroadcastState();
        return;
    }

    // 在大厅
    auto lobby_it = lobby_players.find(fd);
    if (lobby_it != lobby_players.end()) {
        lobby_it->second.name = name;
        lobby_it->second.avatar = avatar;

        // 回复确认
        std::ostringstream ss;
        ss << "{\"type\":\"set_name_ok\",\"name\":\"" << name
           << "\",\"avatar\":" << avatar << "}";
        SendToPlayer(connections, fd, ss.str());
    }
}

// ===================================================================
// RoomManager::HandleReady —— 准备/取消准备
// ===================================================================
void RoomManager::HandleReady(int fd) {
    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    Room& room = rooms[it->second.room_idx];
    if (room.state != RoomState::WAITING) return;

    int idx = it->second.player_idx;
    room.players[idx].is_ready = !room.players[idx].is_ready;

    // 检查是否满 5 人且全部 ready → 开局
    int player_count = 0;
    for (int i = 0; i < 5; ++i) {
        if (room.IsSeatOccupied(i)) player_count++;
    }
    if (player_count == 5 && room.AllReady()) {
        room.StartGame();
    } else {
        room.BroadcastState();
    }
}

// ===================================================================
// RoomManager::HandleSetRounds —— 房主设定局数
// ===================================================================
void RoomManager::HandleSetRounds(int fd, const std::string& json) {
    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    Room& room = rooms[it->second.room_idx];
    int idx = it->second.player_idx;

    // 校验房主
    if (idx != room.owner_seat) {
        SendToPlayer(connections, fd,
                     "{\"type\":\"error\",\"code\":\"NOT_OWNER\",\"message\":\"只有房主可以设置局数\"}");
        return;
    }

    // 解析 rounds
    size_t pos = json.find("\"rounds\"");
    if (pos == std::string::npos) return;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return;
    int rounds = std::stoi(json.substr(pos + 1));

    // 允许 1/3/5/7/10
    if (rounds != 1 && rounds != 3 && rounds != 5 && rounds != 7 && rounds != 10) {
        rounds = 5;
    }

    room.total_rounds = rounds;
    room.BroadcastState();
}

// ===================================================================
// RoomManager::HandleAddBot —— WAITING 状态空位填 AI
// ===================================================================
void RoomManager::HandleAddBot(int room_idx) {
    Room& room = rooms[room_idx];
    if (room.state != RoomState::WAITING) return;

    // 找第一个空位
    int seat = -1;
    for (int i = 0; i < 5; ++i) {
        if (!room.IsSeatOccupied(i)) {
            seat = i;
            break;
        }
    }
    if (seat == -1) return;

    // 统计已有 AI 数量，确定编号
    int bot_count = 0;
    for (int i = 0; i < 5; ++i) {
        if (room.players[i].is_ai) bot_count++;
    }

    room.players[seat].fd = -1;
    room.players[seat].name = "AI_" + std::to_string(bot_count + 1);
    room.players[seat].avatar = 0;
    room.players[seat].is_ready = true;
    room.players[seat].is_ai = true;

    room.BroadcastState();
    BroadcastRoomList();

    if (room.AllReady()) {
        room.StartGame();
    }
}

// ===================================================================
// RoomManager::HandleRemoveBot —— WAITING 状态移除 AI
// ===================================================================
void RoomManager::HandleRemoveBot(int room_idx, int seat) {
    Room& room = rooms[room_idx];
    if (room.state != RoomState::WAITING) return;
    if (seat < 0 || seat >= 5) return;
    if (!room.IsSeatOccupied(seat)) return;

    // 如果是真人，踢回大厅
    int fd = room.players[seat].fd;
    if (fd != -1) {
        std::string name = room.players[seat].name;
        int avatar = room.players[seat].avatar;
        room.players[seat] = PlayerContext{};
        fd_to_location.erase(fd);
        lobby_players[fd] = {name, avatar};
        // 通知该玩家已离开房间
        SendToPlayer(connections, fd,
            "{\"type\":\"kicked\",\"message\":\"你已被移出房间\"}");
    } else {
        room.players[seat] = PlayerContext{};
    }

    // 房主转移
    if (room.owner_seat == seat) {
        room.TransferOwnership();
    }

    // 有人离开（踢人机也算）→ 重置全员分数和局数
    for (int i = 0; i < 5; ++i) {
        room.cumulative_scores[i] = 0;
        room.round_scores[i] = 0;
    }
    room.current_round = 0;

    room.BroadcastState();
    BroadcastRoomList();
}

// ===================================================================
// RoomManager::HandleContinue —— END 结算后推进
// ===================================================================
void RoomManager::HandleContinue(int fd) {
    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    Room& room = rooms[it->second.room_idx];
    if (room.state != RoomState::END) return;

    int idx = it->second.player_idx;

    // 标记该玩家已确认继续
    // 这里简化处理：任一玩家点继续就推进（后面可改为需要所有人确认）
    // 更新累计分数
    bool landlord_win = false;
    for (int i = 0; i < 5; ++i) {
        if (room.players[i].IsHandEmpty() && room.players[i].is_landlord) {
            landlord_win = true;
            break;
        }
    }

    int64_t base = room.multiplier;
    for (int i = 0; i < 5; ++i) {
        int64_t delta = 0;
        if (landlord_win) {
            delta = room.players[i].is_landlord ? (3 * base) : (-2 * base);
        } else {
            delta = room.players[i].is_landlord ? (-3 * base) : (2 * base);
        }
        room.round_scores[i] = delta;
        room.cumulative_scores[i] += delta;
    }

    // 统一回到 WAITING，所有人重新 READY 后开下一局
    room.ReturnToWaiting();
    if (room.current_round >= room.total_rounds) {
        // 末局 → 新比赛，清空累计分
        for (int i = 0; i < 5; ++i) room.cumulative_scores[i] = 0;
        room.current_round = 0;
    }
    room.BroadcastState();
    BroadcastRoomList();
}

// ===================================================================
// RoomManager::OnMessage
// ===================================================================
void RoomManager::OnMessage(int fd, const std::string& json) {
    // ---- RECONNECT ----
    if (json.find("\"RECONNECT\"") != std::string::npos ||
        json.find("\"action\":\"RECONNECT\"") != std::string::npos) {
        size_t pos = json.find("\"token\"");
        if (pos != std::string::npos) {
            pos = json.find('"', pos + 7);
            if (pos != std::string::npos) {
                size_t end = json.find('"', pos + 1);
                if (end != std::string::npos) {
                    std::string token = json.substr(pos + 1, end - pos - 1);
                    HandleReconnect(fd, token);
                    return;
                }
            }
        }
        return;
    }

    // ---- PING ----
    if (json.find("\"PING\"") != std::string::npos) {
        return;
    }

    // ---- SET_NAME (大厅或房间内均可) ----
    if (json.find("\"SET_NAME\"") != std::string::npos ||
        json.find("\"action\":\"SET_NAME\"") != std::string::npos) {
        HandleSetName(fd, json);
        return;
    }

    // ---- 大厅消息 ----
    if (IsInLobby(fd)) {
        if (json.find("\"JOIN_ROOM\"") != std::string::npos ||
            json.find("\"action\":\"JOIN_ROOM\"") != std::string::npos) {
            size_t pos = json.find("\"room_id\"");
            if (pos != std::string::npos) {
                pos = json.find(':', pos);
                if (pos != std::string::npos) {
                    int room_id = std::stoi(json.substr(pos + 1));
                    if (!JoinRoom(fd, room_id)) {
                        SendToPlayer(connections, fd,
                            "{\"type\":\"error\",\"code\":\"ROOM_FULL\",\"message\":\"房间已满或不存在\"}");
                    }
                }
            }
        }
        // ROOM_LIST — re-send current room list to this lobby player
        if (json.find("\"ROOM_LIST\"") != std::string::npos ||
            json.find("\"action\":\"ROOM_LIST\"") != std::string::npos) {
            std::ostringstream ss;
            ss << "{\"type\":\"room_list\",\"rooms\":[";
            for (int i = 0; i < MAX_ROOMS; ++i) {
                if (i > 0) ss << ",";
                ss << rooms[i].SerializeRoomInfo(i + 1);
            }
            ss << "]}";
            SendToPlayer(connections, fd, ss.str());
        }
        return;
    }

    // ---- 房间内消息 ----
    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    Room& room = rooms[it->second.room_idx];

    // LEAVE_ROOM (任何状态都可以离开)
    if (json.find("\"LEAVE_ROOM\"") != std::string::npos ||
        json.find("\"action\":\"LEAVE_ROOM\"") != std::string::npos) {
        LeaveRoom(fd);
        return;
    }

    // SET_SPEED — 切换 AI 思考速度（任何游戏状态）
    if (json.find("\"SET_SPEED\"") != std::string::npos ||
        json.find("\"action\":\"SET_SPEED\"") != std::string::npos) {
        size_t spos = json.find("\"speed\"");
        if (spos != std::string::npos) {
            spos = json.find(':', spos);
            if (spos != std::string::npos) {
                int speed = std::stoi(json.substr(spos + 1));
                room.ai_delay_ms = (speed >= 3) ? 333 : (speed >= 2) ? 1000 : 2000;
            }
        }
        return;
    }

    // SET_AUTOPLAY — 托管开关（任何游戏状态）
    if (json.find("\"SET_AUTOPLAY\"") != std::string::npos ||
        json.find("\"action\":\"SET_AUTOPLAY\"") != std::string::npos) {
        int idx = it->second.player_idx;
        room.players[idx].is_autoplay = !room.players[idx].is_autoplay;
        // 如果开启托管且当前轮到该玩家，立即调度 AI
        if (room.players[idx].is_autoplay) {
            room.ai_scheduled_at = 0;
            CheckAndTriggerAI(it->second.room_idx);
        }
        return;
    }

    // 玩家手动操作时，如果托管中则自动关闭托管
    {
        int idx = it->second.player_idx;
        bool is_game_action = (json.find("\"PLAY\"") != std::string::npos ||
                               json.find("\"PASS\"") != std::string::npos ||
                               json.find("\"CALL\"") != std::string::npos ||
                               json.find("\"PICK_BOTTOM\"") != std::string::npos ||
                               json.find("\"HINT\"") != std::string::npos);
        if (is_game_action && room.players[idx].is_autoplay) {
            room.players[idx].is_autoplay = false;
            room.ai_scheduled_at = 0;
        }
    }

    switch (room.state) {
        case RoomState::WAITING: {
            // READY / CONTINUE（两者等价，toggle 准备状态）
            if (json.find("\"READY\"") != std::string::npos ||
                json.find("\"action\":\"READY\"") != std::string::npos ||
                json.find("\"CONTINUE\"") != std::string::npos ||
                json.find("\"action\":\"CONTINUE\"") != std::string::npos) {
                HandleReady(fd);
            }
            // SET_ROUNDS
            if (json.find("\"SET_ROUNDS\"") != std::string::npos ||
                json.find("\"action\":\"SET_ROUNDS\"") != std::string::npos) {
                HandleSetRounds(fd, json);
            }
            // ADD_BOT — 空位填 AI
            if (json.find("\"ADD_BOT\"") != std::string::npos ||
                json.find("\"action\":\"ADD_BOT\"") != std::string::npos) {
                HandleAddBot(it->second.room_idx);
            }
            // REMOVE_BOT — 移除 AI
            if (json.find("\"REMOVE_BOT\"") != std::string::npos ||
                json.find("\"action\":\"REMOVE_BOT\"") != std::string::npos) {
                int seat = -1;
                size_t spos = json.find("\"seat\"");
                if (spos != std::string::npos) {
                    spos = json.find(':', spos);
                    if (spos != std::string::npos) {
                        seat = std::stoi(json.substr(spos + 1));
                    }
                }
                HandleRemoveBot(it->second.room_idx, seat);
            }
            break;
        }

        case RoomState::BIDDING: {
            room.HandleBidding(fd, json);
            break;
        }

        case RoomState::BOTTOM_PICK: {
            if (json.find("\"PICK_BOTTOM\"") != std::string::npos ||
                json.find("\"action\":\"PICK_BOTTOM\"") != std::string::npos) {
                room.HandlePickBottom(fd, json);
            }
            break;
        }

        case RoomState::PLAYING: {
            if (json.find("\"HINT\"") != std::string::npos ||
                json.find("\"action\":\"HINT\"") != std::string::npos) {
                room.HandleHint(fd);
            } else {
                room.HandlePlaying(fd, json);
            }
            break;
        }

        case RoomState::END: {
            // CONTINUE
            if (json.find("\"CONTINUE\"") != std::string::npos ||
                json.find("\"action\":\"CONTINUE\"") != std::string::npos) {
                HandleContinue(fd);
            }
            break;
        }
    }

    // 消息处理完后，检查当前回合是否为 AI 座位
    if (room.state == RoomState::PLAYING || room.state == RoomState::BIDDING ||
        room.state == RoomState::BOTTOM_PICK) {
        CheckAndTriggerAI(it->second.room_idx);
    }
}

// ===================================================================
// RoomManager::CheckAndTriggerAI
// ===================================================================
void RoomManager::CheckAndTriggerAI(int room_idx) {
    Room& room = rooms[room_idx];

    if (room.state == RoomState::BIDDING) {
        if (room.current_bidder_pos < static_cast<int>(room.bidder_queue.size())) {
            int bidder = room.bidder_queue[room.current_bidder_pos];
            if (room.players[bidder].fd == -1 || room.players[bidder].is_autoplay) {
                if (room.ai_scheduled_at == 0) {
                    room.ai_scheduled_at = NowMs() + room.ai_delay_ms;
                }
            }
        }
        return;
    }

    if (room.state == RoomState::BOTTOM_PICK) {
        if (room.bottom_pick_landlord >= 0 &&
            (room.players[room.bottom_pick_landlord].fd == -1 ||
             room.players[room.bottom_pick_landlord].is_autoplay) &&
            room.bottom_pick_count < 2) {
            if (room.ai_scheduled_at == 0) {
                room.ai_scheduled_at = NowMs() + room.ai_delay_ms;
            }
        }
        return;
    }

    if (room.state == RoomState::PLAYING) {
        int turn = room.current_turn;
        if (!room.players[turn].IsHandEmpty() && (room.players[turn].fd == -1 || room.players[turn].is_autoplay)) {
            if (room.ai_scheduled_at == 0) {
                room.ai_scheduled_at = NowMs() + room.ai_delay_ms;
            }
        }
        return;
    }
}

// ===================================================================
// RoomManager::ProcessScheduledAI —— 主循环调用，到时间则执行 AI
// ===================================================================
void RoomManager::ProcessScheduledAI() {
    int64_t now = NowMs();
    for (int i = 0; i < MAX_ROOMS; ++i) {
        Room& room = rooms[i];
        if (room.ai_scheduled_at == 0 || now < room.ai_scheduled_at) continue;

        room.ai_scheduled_at = 0;

        // 执行当前 AI 行动（立即模式）
        if (room.state == RoomState::BIDDING) {
            if (room.current_bidder_pos < static_cast<int>(room.bidder_queue.size())) {
                int bidder = room.bidder_queue[room.current_bidder_pos];
                if (room.players[bidder].fd == -1 || room.players[bidder].is_autoplay) {
                    room.HandleAIBidding(bidder);
                    CheckAndTriggerAI(i);
                }
            }
        } else if (room.state == RoomState::BOTTOM_PICK) {
            if (room.bottom_pick_landlord >= 0 &&
                (room.players[room.bottom_pick_landlord].fd == -1 ||
                 room.players[room.bottom_pick_landlord].is_autoplay) &&
                room.bottom_pick_count < 2) {
                room.HandleAIPickBottom(room.bottom_pick_landlord);
                CheckAndTriggerAI(i);
            }
        } else if (room.state == RoomState::PLAYING) {
            int turn = room.current_turn;
            if (!room.players[turn].IsHandEmpty() && (room.players[turn].fd == -1 || room.players[turn].is_autoplay)) {
                RequestAIDecision(i, turn);
                CheckAndTriggerAI(i);
            }
        }
    }
}

// ===================================================================
// RoomManager::RequestAIDecision
// ===================================================================
void RoomManager::RequestAIDecision(int room_idx, int player_idx) {
    if (!ipc_client) {
        Room& room = rooms[room_idx];
        if (room.last_player_idx == -1 && !room.players[player_idx].hand.empty()) {
            room.ExecuteAIDecision(player_idx, "PLAY",
                                  {*std::min_element(room.players[player_idx].hand.begin(),
                                                     room.players[player_idx].hand.end())});
        } else {
            auto options = CardRule::GetHints(room.players[player_idx].hand, room.last_played_cards);
            if (!options.empty()) {
                room.ExecuteAIDecision(player_idx, "PLAY", options[0]);
            } else {
                room.ExecuteAIDecision(player_idx, "PASS", {});
            }
        }
        return;
    }

    Room& room = rooms[room_idx];
    const PlayerContext& player = room.players[player_idx];

    std::ostringstream ss;
    ss << "{"
       << "\"room_idx\":" << room_idx
       << ",\"player_idx\":" << player_idx
       << ",\"hand\":[";
    for (size_t i = 0; i < player.hand.size(); ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(player.hand[i]);
    }
    ss << "]"
       << ",\"last_played\":[";
    for (size_t i = 0; i < room.last_played_cards.size(); ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(room.last_played_cards[i]);
    }
    ss << "]"
       << ",\"last_player\":" << room.last_player_idx
       << ",\"is_landlord\":" << (player.is_landlord ? "true" : "false")
       << ",\"multiplier\":" << room.multiplier
       << "}";

    ipc_client->SendSnapshot(ss.str());
}

// ===================================================================
// RoomManager::ApplyAIDecision
// ===================================================================
void RoomManager::ApplyAIDecision(const std::string& json) {
    size_t pos = json.find("\"room_idx\"");
    if (pos == std::string::npos) return;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return;
    int room_idx = std::stoi(json.substr(pos + 1));

    if (room_idx < 0 || room_idx >= MAX_ROOMS) return;

    pos = json.find("\"player_idx\"");
    if (pos == std::string::npos) return;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return;
    int player_idx = std::stoi(json.substr(pos + 1));

    if (player_idx < 0 || player_idx >= 5) return;

    std::string action;
    if (json.find("\"PLAY\"") != std::string::npos) {
        action = "PLAY";
    } else if (json.find("\"PASS\"") != std::string::npos) {
        action = "PASS";
    } else {
        return;
    }

    std::vector<uint8_t> cards;
    if (action == "PLAY") {
        pos = json.find("\"cards\"");
        if (pos != std::string::npos) {
            pos = json.find('[', pos);
            if (pos != std::string::npos) {
                size_t end = json.find(']', pos);
                if (end != std::string::npos) {
                    std::string cards_str = json.substr(pos + 1, end - pos - 1);
                    size_t start = 0;
                    while (start < cards_str.size()) {
                        while (start < cards_str.size() &&
                               (cards_str[start] == ',' || cards_str[start] == ' ')) start++;
                        if (start >= cards_str.size()) break;
                        size_t num_end = start;
                        while (num_end < cards_str.size() &&
                               cards_str[num_end] >= '0' && cards_str[num_end] <= '9') num_end++;
                        if (num_end > start) {
                            cards.push_back(static_cast<uint8_t>(
                                std::stoi(cards_str.substr(start, num_end - start))));
                        }
                        start = num_end;
                    }
                }
            }
        }
    }

    Room& room = rooms[room_idx];
    room.ExecuteAIDecision(player_idx, action, cards);

    CheckAndTriggerAI(room_idx);
}

// ===================================================================
// RoomManager::HandleReconnect
// ===================================================================
bool RoomManager::HandleReconnect(int fd, const std::string& token) {
    if (token.empty()) return false;

    for (int i = 0; i < MAX_ROOMS; ++i) {
        int player_idx = rooms[i].FindPlayerByToken(token);
        if (player_idx == -1) continue;

        // 清理新 fd 的旧状态
        auto old = fd_to_location.find(fd);
        if (old != fd_to_location.end()) {
            rooms[old->second.room_idx].players[old->second.player_idx].fd = -1;
            fd_to_location.erase(old);
        }
        // 清理大厅记录
        lobby_players.erase(fd);

        // 恢复连接，is_ready 保持
        rooms[i].ReconnectPlayer(player_idx, fd);
        fd_to_location[fd] = {i, player_idx};
        BindSendCallback(rooms[i]);

        if (rooms[i].on_send) {
            rooms[i].on_send(fd, rooms[i].SerializeState(player_idx));
        }

        rooms[i].BroadcastState();

        std::cout << "[RoomManager] 玩家 fd=" << fd << " 重连成功 room=" << i
                  << " seat=" << player_idx << std::endl;
        return true;
    }
    return false;
}

// ===================================================================
// RoomManager::GetRoomByFd
// ===================================================================
Room* RoomManager::GetRoomByFd(int fd) {
    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return nullptr;
    return &rooms[it->second.room_idx];
}

// ===================================================================
// RoomManager::GetActiveRoomCount
// ===================================================================
int RoomManager::GetActiveRoomCount() const {
    int count = 0;
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (rooms[i].state != RoomState::WAITING) count++;
    }
    return count;
}
