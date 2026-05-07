#!/usr/bin/env python3
"""
agent_brain.py —— 五人斗地主 AI 托管网关

架构：
  C++ 服务端 ──TCP──▶ agent_brain (本进程) ──HTTP──▶ DeepSeek API
                          │
                          ├─ 2.5s 超时 → 自动 PASS
                          └─ 防幻觉校验 → 出了没有的牌 → PASS

协议：
  接收/发送均使用 4 字节大端长度头 + JSON body（与 C++ 端一致）
"""

import socket
import struct
import json
import time
import sys
import os
import re
from urllib import request, error
from collections import Counter


# ===================================================================
# 牌点映射 —— 卡牌编码 → 人类可读
# ===================================================================
RANK_NAMES = ['3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K', 'A', '2', '小王', '大王']


def card_name(card_id):
    """将 C++ 卡牌编码转为人读名称，如 0→'方3', 56→'大王'"""
    if card_id == 53:
        return '小王'
    if card_id == 56:
        return '大王'
    if card_id <= 51:
        suit_names = ['方', '梅', '红', '黑']
        rank = card_id // 4
        suit = card_id % 4
        return f'{suit_names[suit]}{RANK_NAMES[rank]}'
    return f'未知({card_id})'


def hand_summary(cards):
    """手牌可读摘要"""
    return ' '.join(card_name(c) for c in cards)


# ===================================================================
# LLM Prompt 构建
# ===================================================================
def build_prompt(hand, last_played, last_player, is_landlord, multiplier):
    """构建发给 DeepSeek 的结构化 Prompt"""
    hand_str = hand_summary(hand)
    last_str = hand_summary(last_played) if last_played else '（新一轮，自由出牌）'
    identity = '地主' if is_landlord else '农民'
    is_new_round = (last_player == -1)

    rank_counter = Counter(c // 4 for c in hand if c <= 51)
    has_small_joker = 53 in hand
    has_big_joker = 56 in hand
    bomb_candidates = [RANK_NAMES[r] for r, cnt in rank_counter.items() if cnt >= 3]

    prompt = f"""你是五人斗地主的AI玩家。请根据牌局状态做出最优出牌决策。

## 你的身份
你是【{identity}】。作为{identity}，你的目标是与盟友合作，率先出完所有手牌。

## 你的手牌(名称=编码)
{hand_str}

## 手牌编码对照(出牌时请用编码)
{', '.join(f'{card_name(c)}={c}' for c in hand)}

## 手牌统计
- 共 {len(hand)} 张
- 点分布: {dict(rank_counter)}
- 有小王: {'是' if has_small_joker else '否'}
- 有大王: {'是' if has_big_joker else '否'}
- 可组成炸弹的点数: {bomb_candidates if bomb_candidates else '无'}

## 桌面状态
- {"新一轮自由出牌" if is_new_round else f"需要压制: {last_str}"}
  (需压制的牌编码: {last_played})

## 规则摘要
- 牌型: 单张/对子/三带一/三带二/顺子(>=3张连续,不含2和王)/连对(>=2对连续)/飞机/四带二/炸弹(3或4张同点)/王炸
- 压制: 同类型比点数大小; 顺子可同起点更长或更高起点; 炸弹可压任何普通牌型; 王炸最大
- 如果你不能或不想出牌，返回 PASS

## 出牌决策
严格返回JSON(只返回JSON):
{{"action":"PLAY","cards":[编码列表]}}
或
{{"action":"PASS"}}

注意:
- 出牌编码必须从上方「手牌编码对照」中选择，绝对不能编造不存在的编码
- 新一轮自由出牌时绝对不能PASS
- 农民优先出小牌消耗地主大牌; 地主优先出大牌压制
"""
    return prompt


# ===================================================================
# DeepSeek API 调用
# ===================================================================
def call_deepseek(prompt, api_key, timeout=2.5):
    """调用 DeepSeek API，返回模型回复文本。失败返回 None"""
    url = "https://api.deepseek.com/v1/chat/completions"
    body = json.dumps({
        "model": "deepseek-chat",
        "messages": [
            {"role": "system", "content": "你是一个精通斗地主的AI玩家。你只返回JSON，不返回其他内容。"},
            {"role": "user", "content": prompt}
        ],
        "temperature": 0.3,
        "max_tokens": 512,
        "stream": False
    }).encode('utf-8')

    req = request.Request(url, data=body)
    req.add_header('Content-Type', 'application/json')
    req.add_header('Authorization', f'Bearer {api_key}')

    try:
        resp = request.urlopen(req, timeout=timeout)
        data = json.loads(resp.read().decode('utf-8'))
        return data['choices'][0]['message']['content']
    except error.HTTPError as e:
        body = e.read().decode('utf-8', errors='ignore')[:200]
        print(f"[agent] DeepSeek HTTP {e.code}: {body}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"[agent] DeepSeek 调用失败: {e}", file=sys.stderr)
        return None


# ===================================================================
# 响应解析
# ===================================================================
def parse_decision(text, hand, is_new_round):
    """从 LLM 回复中提取决策。失败返回 fallback"""
    if not text:
        return fallback_decision(hand, is_new_round)

    text = text.strip()
    text = re.sub(r'^```(?:json)?\s*', '', text)
    text = re.sub(r'\s*```$', '', text)

    try:
        decision = json.loads(text)
        action = decision.get('action', '')

        if action == 'PASS':
            if is_new_round:
                return fallback_decision(hand, is_new_round)
            return {'action': 'PASS'}

        if action == 'PLAY':
            cards = decision.get('cards', [])
            if not cards:
                return fallback_decision(hand, is_new_round)
            for c in cards:
                if c not in hand:
                    print(f"[agent] 防幻觉拦截: AI 出了不在手牌的牌 (编码{c})", file=sys.stderr)
                    return fallback_decision(hand, is_new_round)
            return {'action': 'PLAY', 'cards': cards}

    except (json.JSONDecodeError, TypeError, KeyError) as e:
        print(f"[agent] JSON 解析失败: {e}", file=sys.stderr)

    return fallback_decision(hand, is_new_round)


def fallback_decision(hand, is_new_round):
    """兜底策略"""
    if is_new_round:
        if hand:
            min_card = min(hand)
            return {'action': 'PLAY', 'cards': [min_card]}
        return {'action': 'PASS'}
    return {'action': 'PASS'}


# ===================================================================
# TCP 协议层 —— 4字节大端长度头 + JSON body
# ===================================================================
def recv_message(sock):
    """从 socket 读取一条完整的 length-prefixed 消息"""
    header = b''
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            return None
        header += chunk
    body_len = struct.unpack('!I', header)[0]

    if body_len > 64 * 1024:
        print(f"[agent] 超大包 {body_len} 字节，断开", file=sys.stderr)
        return None

    body = b''
    while len(body) < body_len:
        chunk = sock.recv(body_len - len(body))
        if not chunk:
            return None
        body += chunk
    return body.decode('utf-8')


def send_message(sock, msg):
    """发送一条 length-prefixed 消息"""
    body = msg.encode('utf-8') if isinstance(msg, str) else msg
    header = struct.pack('!I', len(body))
    sock.sendall(header + body)


# ===================================================================
# 主循环
# ===================================================================
def load_api_key():
    """从 .env 文件或环境变量加载 API key（.env 优先）"""
    # 1. 尝试同目录下的 .env 文件
    env_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.env')
    if os.path.exists(env_file):
        with open(env_file, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith('#') or '=' not in line:
                    continue
                k, v = line.split('=', 1)
                if k.strip() == 'DEEPSEEK_API_KEY':
                    return v.strip().strip('"').strip("'")
    # 2. 回退到环境变量
    return os.environ.get('DEEPSEEK_API_KEY', '')


def main():
    api_key = load_api_key()
    if not api_key:
        print("[agent] 警告: DEEPSEEK_API_KEY 未设置，将始终使用 fallback (PASS)", file=sys.stderr)

    host = '127.0.0.1'
    port = 8081

    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((host, port))
    server_sock.listen(1)

    print(f"[agent] AI 大脑已启动，监听 {host}:{port}")
    print(f"[agent] DeepSeek API: {'已配置' if api_key else '未配置 (fallback模式)'}")

    while True:
        print("[agent] 等待 C++ 服务端连接...")
        conn, addr = server_sock.accept()
        print(f"[agent] C++ 服务端已连接: {addr}")

        try:
            while True:
                msg = recv_message(conn)
                if msg is None:
                    print("[agent] C++ 连接断开")
                    break

                try:
                    req = json.loads(msg)
                except json.JSONDecodeError:
                    print(f"[agent] 无效 JSON: {msg[:100]}", file=sys.stderr)
                    continue

                hand = req.get('hand', [])
                last_played = req.get('last_played', [])
                last_player = req.get('last_player', -1)
                is_landlord = req.get('is_landlord', False)
                multiplier = req.get('multiplier', 1)
                is_new_round = (last_player == -1)

                t0 = time.time()

                if api_key:
                    prompt = build_prompt(hand, last_played, last_player, is_landlord, multiplier)
                    llm_response = call_deepseek(prompt, api_key, timeout=2.5)
                    decision = parse_decision(llm_response, hand, is_new_round)
                else:
                    decision = fallback_decision(hand, is_new_round)

                elapsed = time.time() - t0
                cards_str = ','.join(str(c) for c in decision.get('cards', []))
                print(f"[agent] 决策: {decision['action']} [{cards_str}] ({elapsed:.2f}s)")

                resp = {
                    'room_idx': req.get('room_idx', -1),
                    'player_idx': req.get('player_idx', -1),
                    'action': decision['action'],
                    'cards': decision.get('cards', [])
                }
                send_message(conn, json.dumps(resp))

        except (ConnectionResetError, BrokenPipeError, OSError) as e:
            print(f"[agent] 连接异常: {e}")
        finally:
            conn.close()


if __name__ == '__main__':
    main()
