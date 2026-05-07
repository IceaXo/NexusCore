#include "RoomManager.h"
#include "../network/Connection.h"
#include "../ipc/IPCClient.h"
#include "CardRule.h"
#include <iostream>
#include <sstream>
#include <cstring>     // std::memcpy
#include <arpa/inet.h> // htonl

// ===================================================================
// RoomManager::RoomManager —— 预分配 2000 房间对象池
// ===================================================================
RoomManager::RoomManager() {
    rooms.reserve(MAX_ROOMS);
    for (int i = 0; i < MAX_ROOMS; ++i) {
        rooms.emplace_back();
    }
}

// ===================================================================
// RoomManager::SetConnectionMap
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

        // 打包：[4字节大端长度头][JSON body]
        uint32_t net_len = htonl(static_cast<uint32_t>(json.size()));
        std::string packet(4, '\0');
        std::memcpy(&packet[0], &net_len, 4);
        packet.append(json);

        it->second.send_buffer.append(packet);
        it->second.WriteToSocket();
    };
}

// ===================================================================
// RoomManager::FindWaitingRoom
// ===================================================================
int RoomManager::FindWaitingRoom() {
    // 优先找 WAITING 房间
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (rooms[i].state == RoomState::WAITING) {
            for (int j = 0; j < 5; ++j) {
                if (rooms[i].players[j].fd == -1) return i;
            }
        }
    }
    // 回收 END 房间
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
        if (room.players[i].fd == -1) return i;
    }
    return -1;
}

// ===================================================================
// RoomManager::AddPlayer
// ===================================================================
bool RoomManager::AddPlayer(int fd) {
    // 先检查是否已在某房间（重连场景先清理旧座位）
    auto existing = fd_to_location.find(fd);
    if (existing != fd_to_location.end()) {
        RemovePlayer(fd);
    }

    int room_idx = FindWaitingRoom();
    if (room_idx == -1) {
        std::cerr << "[RoomManager] 所有房间已满，拒绝玩家 fd=" << fd << std::endl;
        return false;
    }

    int seat = FindEmptySeat(rooms[room_idx]);
    if (seat == -1) return false; // 理论上不会到这里

    Room& room = rooms[room_idx];
    PlayerContext& player = room.players[seat];
    player.fd = fd;
    player.hand.clear();
    player.is_landlord = false;
    player.has_passed_bidding = false;

    fd_to_location[fd] = {room_idx, seat};
    BindSendCallback(room);

    // 检查是否满 5 人 → 开局
    int player_count = 0;
    for (int i = 0; i < 5; ++i) {
        if (room.players[i].fd != -1) player_count++;
    }
    if (player_count == 5) {
        room.StartGame();
    }

    return true;
}

// ===================================================================
// RoomManager::RemovePlayer
// ===================================================================
void RoomManager::RemovePlayer(int fd) {
    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    int room_idx = it->second.room_idx;
    int player_idx = it->second.player_idx;
    fd_to_location.erase(it);

    Room& room = rooms[room_idx];

    switch (room.state) {
        case RoomState::WAITING: {
            room.players[player_idx].fd = -1;
            break;
        }
        case RoomState::BIDDING: {
            // 如果断线的是当前正在叫牌的候选人 → 自动 PASS
            if (static_cast<int>(room.bidder_queue.size()) > room.current_bidder_pos &&
                room.bidder_queue[room.current_bidder_pos] == player_idx) {
                // 从 fd_to_location 已删，HandleBidding 用 -1 fd 调 GetPlayerIndex 会返回 -1
                // 直接模拟 PASS
                room.players[player_idx].fd = -1;
                room.HandleBidding(fd, "PASS");
            } else {
                room.players[player_idx].fd = -1;
            }
            break;
        }
        case RoomState::PLAYING: {
            room.SetAITakeover(player_idx);
            break;
        }
        case RoomState::END: {
            room.players[player_idx].fd = -1;
            break;
        }
    }
}

// ===================================================================
// RoomManager::OnMessage
// ===================================================================
void RoomManager::OnMessage(int fd, const std::string& json) {
    // 优先处理 RECONNECT 消息（玩家可能在 WAITING 房间，尚未入局）
    if (json.find("\"RECONNECT\"") != std::string::npos ||
        json.find("\"action\":\"RECONNECT\"") != std::string::npos) {
        // 提取 token
        size_t pos = json.find("\"token\"");
        if (pos != std::string::npos) {
            pos = json.find('"', pos + 7);  // 跳过 "token"
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

    auto it = fd_to_location.find(fd);
    if (it == fd_to_location.end()) return;

    Room& room = rooms[it->second.room_idx];

    switch (room.state) {
        case RoomState::WAITING:
            break; // 房间不满员，忽略消息

        case RoomState::BIDDING: {
            // 提取 action: "CALL" 或 "PASS"
            std::string action;
            if (json.find("\"CALL\"") != std::string::npos ||
                json.find("\"action\":\"CALL\"") != std::string::npos) {
                action = "CALL";
            } else if (json.find("\"PASS\"") != std::string::npos ||
                       json.find("\"action\":\"PASS\"") != std::string::npos) {
                action = "PASS";
            } else {
                return;
            }
            room.HandleBidding(fd, action);
            break;
        }

        case RoomState::PLAYING: {
            // 提示消息单独处理
            if (json.find("\"HINT\"") != std::string::npos ||
                json.find("\"action\":\"HINT\"") != std::string::npos) {
                room.HandleHint(fd);
            } else {
                room.HandlePlaying(fd, json);
            }
            break;
        }

        case RoomState::END:
            break; // 游戏结束，忽略操作
    }

    // 消息处理完后，检查当前回合是否为 AI 座位
    if (room.state == RoomState::PLAYING || room.state == RoomState::BIDDING) {
        CheckAndTriggerAI(it->second.room_idx);
    }
}

// ===================================================================
// RoomManager::CheckAndTriggerAI —— 检查并触发 AI 决策
// ===================================================================
void RoomManager::CheckAndTriggerAI(int room_idx) {
    Room& room = rooms[room_idx];

    if (room.state == RoomState::BIDDING) {
        // 当前叫地主候选人是 AI → 自动 PASS
        if (room.current_bidder_pos < static_cast<int>(room.bidder_queue.size())) {
            int bidder = room.bidder_queue[room.current_bidder_pos];
            if (room.players[bidder].fd == -1) {
                room.HandleAIBidding(bidder);
                // 递归检查：AI PASS 后下一位可能又是 AI
                CheckAndTriggerAI(room_idx);
            }
        }
        return;
    }

    if (room.state == RoomState::PLAYING) {
        int turn = room.current_turn;
        if (!room.players[turn].IsHandEmpty() && room.players[turn].fd == -1) {
            RequestAIDecision(room_idx, turn);
        }
        return;
    }
}

// ===================================================================
// RoomManager::RequestAIDecision —— 向 Python AI 发送决策请求
// ===================================================================
void RoomManager::RequestAIDecision(int room_idx, int player_idx) {
    if (!ipc_client) return;

    Room& room = rooms[room_idx];
    const PlayerContext& player = room.players[player_idx];

    // 构建 AI 请求 JSON
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
// RoomManager::ApplyAIDecision —— 收到 AI 决策回包后执行
// ===================================================================
void RoomManager::ApplyAIDecision(const std::string& json) {
    // 解析 room_idx
    size_t pos = json.find("\"room_idx\"");
    if (pos == std::string::npos) return;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return;
    int room_idx = std::stoi(json.substr(pos + 1));

    if (room_idx < 0 || room_idx >= MAX_ROOMS) return;

    // 解析 player_idx
    pos = json.find("\"player_idx\"");
    if (pos == std::string::npos) return;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return;
    int player_idx = std::stoi(json.substr(pos + 1));

    if (player_idx < 0 || player_idx >= 5) return;

    // 解析 action
    std::string action;
    if (json.find("\"PLAY\"") != std::string::npos) {
        action = "PLAY";
    } else if (json.find("\"PASS\"") != std::string::npos) {
        action = "PASS";
    } else {
        return;
    }

    // 解析 cards（仅 PLAY 时）
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

    // 执行
    Room& room = rooms[room_idx];
    room.ExecuteAIDecision(player_idx, action, cards);

    // 执行后可能又轮到下一个 AI，递归检查
    CheckAndTriggerAI(room_idx);
}

// ===================================================================
// RoomManager::HandleReconnect —— 断线重连
// ===================================================================
bool RoomManager::HandleReconnect(int fd, const std::string& token) {
    if (token.empty()) return false;

    // 遍历所有房间查找匹配 token 的断线座位
    for (int i = 0; i < MAX_ROOMS; ++i) {
        int player_idx = rooms[i].FindPlayerByToken(token);
        if (player_idx == -1) continue;

        // 清理新 fd 被 AddPlayer 临时分配的座位（重连先走 Accept→AddPlayer）
        auto old = fd_to_location.find(fd);
        if (old != fd_to_location.end()) {
            rooms[old->second.room_idx].players[old->second.player_idx].fd = -1;
            fd_to_location.erase(old);
        }

        // 恢复连接
        rooms[i].ReconnectPlayer(player_idx, fd);
        fd_to_location[fd] = {i, player_idx};
        BindSendCallback(rooms[i]);

        // 推送当前状态给重连玩家
        if (rooms[i].on_send) {
            rooms[i].on_send(fd, rooms[i].SerializeState(player_idx));
        }

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
