#include "CardRule.h"

// ===================================================================
// EvaluateType —— 牌型判定
// ===================================================================
CardType CardRule::EvaluateType(const std::vector<uint8_t>& cards) {
    // 步骤 1：建频次账本。int freq[15] = {0}，遍历 cards，
    // 对每张牌 card，freq[card / 4]++，同时顺手记下 size 和是否含王。
    int freq[15] = {0};
    int size = 0;
    bool joker = false;
    for (uint8_t c:cards) {
        freq[c/4]++;
        size++;
    }
    if (freq[14]!=0||freq[13]!=0) joker = true;
    // 步骤 2：王炸特判。如果 size == 2 且 freq[13] == 1 且 freq[14] == 1
    // → 立刻返回 ROCKET。这是牌型链的顶端，不需要走后续分支。
    if (size==2&&freq[13]==1&&freq[14]==1) return CardType::ROCKET;
    // 步骤 3：炸弹嗅探。遍历 freq[0..12]（不含王）：
    // 如果任意 count == 4 且 size == 4 → 纯四张炸弹 BOMB
    // 如果任意 count == 3 且 size == 3 → 三张炸弹 BOMB
    // 炸弹优先于三带一被识别（3 张同点就是炸弹，不是三带一缺 kicker）。
    for (int r = 0; r <= 12; ++r) {
        if (freq[r] == 4 && size == 4) return CardType::BOMB;
        if (freq[r] == 3 && size == 3) return CardType::BOMB;
    }

    // 步骤 4：按总张数分流判定。各 case 只判定该张数专属的复合牌型；
    // 命中即 return，未命中则 break 落到 switch 之后的顺子/连对通用判定。
    switch (size) {
    case 1:
        return CardType::SINGLE;

    case 2: {
        for (int r = 0; r <= 14; ++r)
            if (freq[r] == 2) return CardType::PAIR;
        return CardType::INVALID;
    }

    case 4: {
        for (int r = 0; r <= 12; ++r)
            if (freq[r] >= 3) return CardType::TRIPLE_ONE;
        break;
    }

    case 5: {
        int triple = -1, pair = -1;
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] >= 3) triple = r;
            else if (freq[r] >= 2) pair = r;
        }
        if (triple != -1 && pair != -1) return CardType::TRIPLE_TWO;
        break;
    }

    case 6: {
        for (int r = 0; r <= 12; ++r)
            if (freq[r] >= 4) return CardType::QUAD_TWO;
        break;
    }

    case 8: {
        // 四带两对: 四张同点 + 两个对子
        // 若有多个四张 (如 5555+6666), 每个都可作为主体，必须全部检查
        for (int bodyR = 0; bodyR <= 12; ++bodyR) {
            if (freq[bodyR] < 4) continue;
            int pairs = 0;
            for (int r = 0; r <= 14; ++r) {
                if (r == bodyR) continue; // 主体占用 4 张，剩余为 0 (非王最大 4 张)
                pairs += freq[r] / 2;
            }
            if (pairs >= 2) return CardType::QUAD_TWO_PAIRS;
        }
        break;
    }

    default:
        break;
    }

    // 步骤 5：飞机判定。K>=2 组连续三张 (rank 0~11)，可纯/带单翅(K)/带对翅(K)。
    // 优先尝试纯飞机 (3K)，再单翅 (4K)，再对翅 (5K)。
    if (size >= 6) {
        for (int ki = 0; ki < 3; ++ki) {
            int K = 0;
            if (ki == 0 && size % 3 == 0)      K = size / 3;
            else if (ki == 1 && size % 4 == 0)  K = size / 4;
            else if (ki == 2 && size % 5 == 0)  K = size / 5;
            if (K < 2) continue;
            int remaining = size - K * 3;
            if (remaining != 0 && remaining != K && remaining != 2 * K) continue;

            for (int lo = 0; lo + K <= 12; ++lo) {
                bool ok = true;
                for (int r = lo; r < lo + K; ++r)
                    if (freq[r] < 3) { ok = false; break; }
                if (!ok) continue;

                if (remaining == 0) return CardType::AIRPLANE; // 纯飞机

                // 统计除主体三张外的剩余牌
                int extra = 0;
                for (int r = 0; r <= 14; ++r) {
                    int used = (r >= lo && r < lo + K) ? 3 : 0;
                    extra += freq[r] - used;
                }
                if (remaining == K && extra >= K) return CardType::AIRPLANE; // 带单翅
                if (remaining == 2 * K && extra >= 2 * K) {
                    int pairs = 0;
                    for (int r = 0; r <= 14; ++r) {
                        int used = (r >= lo && r < lo + K) ? 3 : 0;
                        pairs += (freq[r] - used) / 2;
                    }
                    if (pairs >= K) return CardType::AIRPLANE; // 带对翅
                }
            }
        }
    }

    // 步骤 6：顺子判定。size >= 3，不含王不含2。
    // 找 freq[0..11] 的首个和末个非零 rank，
    // 若区间内每个 rank count>=1 且区间长度恰好等于 size → STRAIGHT。
    if (!joker && size >= 3) {
        int lo = -1, hi = -1;
        for (int r = 0; r <= 11; ++r) {
            if (freq[r]) { if (lo == -1) lo = r; hi = r; }
        }
        if (lo != -1 && (hi - lo + 1) == size) {
            bool ok = true;
            for (int r = lo; r <= hi; ++r)
                if (freq[r] < 1) { ok = false; break; }
            if (ok) return CardType::STRAIGHT;
        }
    }

    // 步骤 6：连对判定。size >= 4 且为偶数，不含王不含2。
    // 每对 2 张，连续对数 = size / 2。
    // 区间内每个 rank count>=2，区间长度 == 对数 → CONSECUTIVE_PAIRS。
    if (!joker && size >= 4 && size % 2 == 0) {
        int lo = -1, hi = -1;
        for (int r = 0; r <= 11; ++r) {
            if (freq[r] >= 2) { if (lo == -1) lo = r; hi = r; }
        }
        int pairs = size / 2;
        if (lo != -1 && (hi - lo + 1) == pairs) {
            bool ok = true;
            for (int r = lo; r <= hi; ++r)
                if (freq[r] < 2) { ok = false; break; }
            if (ok) return CardType::CONSECUTIVE_PAIRS;
        }
    }

    // 步骤 7：一票否决。上述都不满足 → 返回 INVALID。
    return CardType::INVALID;
}

// ===================================================================
// CanBeat —— 压制判定
// ===================================================================
// 提取一手牌的主体点数 (用于压制比较)。
// 三带一/三带二的主体是 triple 的点数，四带二是 quad 的点数，
// 顺子/连对/飞机取连续序列的起点 (最小点数)。
//
// 三带/四带类采用倒序扫描 (r=14→0)：取最大符合条件点数作主体。
// 防止降维打击 —— 若手牌含 555+666，倒序确保以 6 为主体，不会因正向扫
// 先撞到 5 而丧失压制机会。
static int BodyRank(const std::vector<uint8_t>& cards, CardType type) {
    if (type == CardType::ROCKET) return 14;
    int freq[15] = {0};
    for (uint8_t c : cards) freq[c / 4]++;
    if (type == CardType::TRIPLE_ONE || type == CardType::TRIPLE_TWO) {
        for (int r = 14; r >= 0; --r)
            if (freq[r] >= 3) return r;
    }
    if (type == CardType::QUAD_TWO || type == CardType::QUAD_TWO_PAIRS) {
        for (int r = 14; r >= 0; --r)
            if (freq[r] >= 4) return r;
    }
    // 飞机：找连续 triple 序列的起点，排除翅牌干扰
    if (type == CardType::AIRPLANE) {
        for (int lo = 0; lo <= 11; ++lo) {
            int k = 0;
            while (lo + k <= 11 && freq[lo + k] >= 3) ++k;
            if (k >= 2) return lo;
        }
        return 0;
    }
    // 顺子/连对：连续序列的最小点数
    if (type == CardType::STRAIGHT || type == CardType::CONSECUTIVE_PAIRS) {
        int mn = 14;
        for (uint8_t c : cards) { int r = c / 4; if (r < mn) mn = r; }
        return mn;
    }
    // 单张/对子/炸弹：任一点数即牌力
    return cards[0] / 4;
}

// ---- 飞机结构枚举：穷举一副牌所有可能的 (连续组数K, 翅型, 主体起点) ----
//
// 同一组牌可能对应多种飞机解释。例如 555+666+777+888 (12张, 5678 全是三张)：
//   K=4, wingKind=3 (纯飞机):    body=5678,         3*4=12
//   K=3, wingKind=4 (单翅):      body=567, 翅=888,  3*3+3=12
//   K=3, wingKind=4 (单翅):      body=678, 翅=555,  3*3+3=12
//   K=2, wingKind=5 (对翅):      body=56,  翅=77+88,3*2+4=10 ≠12 ✗
// 全部合法解释都会被枚举，CanBeat 逐解比对以找到压制路径。
//
// wingKind: 3=纯飞机(无翅), 4=带单翅(每主体带1单张), 5=带对翅(每主体带1对)
// 判定逻辑与 EvaluateType 的飞机部分一致
struct AirplaneInterp { int K, wingKind, bodyLo; };
static std::vector<AirplaneInterp> GetAirplaneInterpretations(const std::vector<uint8_t>& cards) {
    int f[15] = {0};
    for (uint8_t c : cards) f[c / 4]++;
    int size = (int)cards.size();
    std::vector<AirplaneInterp> result;
    for (int wingKind = 3; wingKind <= 5; ++wingKind) {
        if (size % wingKind != 0) continue;
        int K = size / wingKind;
        if (K < 2) continue;
        int remaining = size - K * 3;
        for (int lo = 0; lo + K <= 12; ++lo) {
            bool ok = true;
            for (int r = lo; r < lo + K; ++r)
                if (f[r] < 3) { ok = false; break; }
            if (!ok) continue;
            int extra = 0, pairs = 0;
            for (int r = 0; r <= 14; ++r) {
                int used = (r >= lo && r < lo + K) ? 3 : 0;
                int rem = f[r] - used;
                if (rem < 0) { ok = false; break; }
                extra += rem;
                pairs += rem / 2;
            }
            if (!ok) continue;
            if (remaining == 0)
                result.push_back({K, wingKind, lo});
            else if (remaining == K && extra >= K)
                result.push_back({K, wingKind, lo});
            else if (remaining == 2 * K && pairs >= K)
                result.push_back({K, wingKind, lo});
        }
    }
    return result;
}

// ---- 四带两对主体枚举：返回所有可作为主体的点数 ----
//
// 为什么需要枚举多个主体？考虑 8 张牌 5555+6666：
//   方案 A：以 5 为主体 (5555) + 对子来自 6666 (两对 66)
//   方案 B：以 6 为主体 (6666) + 对子来自 5555 (两对 55)
// 两种解释物理上都合法。本函数返回所有可行解，由上层（CanBeat/GetHints）
// 逐解比对，不遗漏任一玩家可选策略。
//
// 算法：遍历 bodyR=0..12 作为候选主体，对每个候选——
//   1. 必须有 ≥4 张 (freq[bodyR] >= 4)
//   2. 累加其余点数的对子数: Σ freq[r] / 2  (主体点数直接跳过，非王最大 4 张已全占)
//   3. 对子 ≥ 2 → 该候选是合法主体
//
// 示例：5555+6666 (freq[5]=4, freq[6]=4)
//   bodyR=5: 5 跳过, freq[6]/2=2 → pairs=2 ✓ → 添加 5
//   bodyR=6: 6 跳过, freq[5]/2=2 → pairs=2 ✓ → 添加 6
// 返回 [5, 6]，玩家可任选其一作为主体
static std::vector<int> GetQuadBodyRanks(const std::vector<uint8_t>& cards) {
    int f[15] = {0};
    for (uint8_t c : cards) f[c / 4]++;
    std::vector<int> ranks;
    for (int bodyR = 0; bodyR <= 12; ++bodyR) {
        if (f[bodyR] < 4) continue;
        int pairs = 0;
        for (int r = 0; r <= 14; ++r) {
            if (r == bodyR) continue; // 主体占用 4 张, 剩余为 0
            pairs += f[r] / 2;
        }
        if (pairs >= 2) ranks.push_back(bodyR);
    }
    return ranks;
}

bool CardRule::CanBeat(const std::vector<uint8_t>& play_cards,
                       const std::vector<uint8_t>& last_cards) {
    CardType play_type = EvaluateType(play_cards);
    CardType last_type = EvaluateType(last_cards);
    if (play_type == CardType::INVALID || last_type == CardType::INVALID)
        return false;

    if (play_type == CardType::ROCKET) return true;
    if (last_type == CardType::ROCKET) return false;

    if (play_type == CardType::BOMB) {
        if (last_type != CardType::BOMB) return true;
        if (play_cards.size() != last_cards.size())
            return play_cards.size() > last_cards.size();
        return BodyRank(play_cards, play_type) > BodyRank(last_cards, last_type);
    }

    // 顺子：最小点数低的一方绝不能压高的一方（拦短压长）
    // pm < lm → false; pm == lm → 比长度; pm > lm → 长度至少相等
    if (play_type == CardType::STRAIGHT && last_type == CardType::STRAIGHT) {
        int pm = BodyRank(play_cards, play_type);
        int lm = BodyRank(last_cards, last_type);
        if (pm < lm) return false;
        return pm == lm ? play_cards.size() > last_cards.size()
                        : play_cards.size() >= last_cards.size();
    }

    // 飞机：任一种相同 (K, 翅型) 解释下，主体起点更高即可压制
    if (play_type == CardType::AIRPLANE && last_type == CardType::AIRPLANE) {
        auto pi = GetAirplaneInterpretations(play_cards);
        auto li = GetAirplaneInterpretations(last_cards);
        for (auto& p : pi)
            for (auto& l : li)
                if (p.K == l.K && p.wingKind == l.wingKind && p.bodyLo > l.bodyLo)
                    return true;
        return false;
    }

    // 四带两对：多四张时 (如 5555+6666) 任一种主体更高即可压制
    if (play_type == CardType::QUAD_TWO_PAIRS && last_type == CardType::QUAD_TWO_PAIRS) {
        auto pBodies = GetQuadBodyRanks(play_cards);
        auto lBodies = GetQuadBodyRanks(last_cards);
        for (int pb : pBodies)
            for (int lb : lBodies)
                if (pb > lb) return true;
        return false;
    }

    if (play_type != last_type || play_cards.size() != last_cards.size())
        return false;

    return BodyRank(play_cards, play_type) > BodyRank(last_cards, last_type);
}

// ===================================================================
// SortHand —— 手牌排序
// ===================================================================
void CardRule::SortHand(std::vector<uint8_t>& hand) {
    // 排序规则：先按逻辑点数升序，同点数再按花色升序（方<梅<红<黑），王自然排在末尾
    //
    // 卡牌编码：card = rank*4 + suit
    //   rank = card / 4   (0=3, 1=4, ..., 11=A, 12=2, 13=小王, 14=大王)
    //   suit = card % 4   (0=方, 1=梅, 2=红, 3=黑)
    //
    // lambda 形参 a, b 是两张牌的编码值：
    //   ra, rb — 先除 4 取出点数，点数不同则按点数排
    //   a%4, b%4 — 点数相同时取余数（花色），按花色排
    // 效果：手牌按 3333 4444 ... AAA 222 小王 大王 分组排列，视觉整齐，便于 AI 扫描
    std::sort(hand.begin(), hand.end(),
        [](uint8_t a, uint8_t b) {
            int ra = a / 4, rb = b / 4;
            if (ra != rb) return ra < rb;
            return (a % 4) < (b % 4);
        });
}

// ===================================================================
// GetHints —— 提示生成
// ===================================================================
// ---- 牌力排序用 ----
static int TypeOrder(CardType t) {
    switch (t) {
    case CardType::SINGLE:            return 0;
    case CardType::PAIR:              return 1;
    case CardType::STRAIGHT:          return 2;
    case CardType::CONSECUTIVE_PAIRS: return 3;
    case CardType::TRIPLE_ONE:        return 4;
    case CardType::TRIPLE_TWO:        return 5;
    case CardType::QUAD_TWO:          return 6;
    case CardType::QUAD_TWO_PAIRS:    return 7;
    case CardType::AIRPLANE:          return 8;
    case CardType::BOMB:              return 9;
    case CardType::ROCKET:            return 10;
    default:                          return 11;
    }
}

std::vector<std::vector<uint8_t>> CardRule::GetHints(
    const std::vector<uint8_t>& hand,
    const std::vector<uint8_t>& last_cards) {

    int freq[15] = {0};
    for (uint8_t c : hand) freq[c / 4]++;

    CardType target_type = CardType::INVALID;
    int target_rank = -1;   // 主体点数
    int target_size = 0;
    if (!last_cards.empty()) {
        target_type = EvaluateType(last_cards);
        target_size = (int)last_cards.size();
        target_rank = BodyRank(last_cards, target_type);
    }

    // 王炸封死
    if (target_type == CardType::ROCKET) return {};

    std::vector<std::vector<uint8_t>> result;

    // ---- 工具 lambda：从手牌中取某个点数的 n 张 ----
    auto take = [&](int r, int n) {
        std::vector<uint8_t> out;
        for (uint8_t c : hand)
            if (c / 4 == r && (int)out.size() < n)
                out.push_back(c);
        return out;
    };

    // ---- 工具：追加一条组合（已按点数升序排好） ----
    auto add = [&](std::vector<uint8_t> v) {
        if (!v.empty()) {
            std::sort(v.begin(), v.end(),
                [](uint8_t a, uint8_t b) { return a / 4 < b / 4; });
            result.push_back(std::move(v));
        }
    };

    // ===========================================================
    // 场景 A：自由出牌 (last_cards 为空)
    // ===========================================================
    if (last_cards.empty()) {
        // A1. 单张
        for (int r = 0; r <= 14; ++r)
            if (freq[r] >= 1) add(take(r, 1));

        // A2. 对子
        for (int r = 0; r <= 14; ++r)
            if (freq[r] >= 2) add(take(r, 2));

        // A3. 顺子 (长度 3~12，rank 0~11，即 3~A；不含2和王)
        for (int len = 3; len <= 12; ++len) {
            for (int lo = 0; lo + len <= 12; ++lo) {
                bool ok = true;
                for (int r = lo; r < lo + len; ++r)
                    if (freq[r] < 1) { ok = false; break; }
                if (ok) {
                    std::vector<uint8_t> v;
                    for (int r = lo; r < lo + len; ++r) {
                        auto t = take(r, 1);
                        v.insert(v.end(), t.begin(), t.end());
                    }
                    add(std::move(v));
                }
            }
        }

        // A4. 连对 (长度 2~K 对，rank 0~11，即3~A；不含2和王)
        for (int pairs = 2; pairs <= 12; ++pairs) {
            for (int lo = 0; lo + pairs <= 12; ++lo) {
                bool ok = true;
                for (int r = lo; r < lo + pairs; ++r)
                    if (freq[r] < 2) { ok = false; break; }
                if (ok) {
                    std::vector<uint8_t> v;
                    for (int r = lo; r < lo + pairs; ++r) {
                        auto t = take(r, 2);
                        v.insert(v.end(), t.begin(), t.end());
                    }
                    add(std::move(v));
                }
            }
        }

        // A5. 三带一
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] >= 3) {
                auto body = take(r, 3);
                // 找一个 kicker（不能是 body 里的 3 张）
                for (uint8_t c : hand) {
                    bool used = false;
                    for (auto b : body) if (c == b) { used = true; break; }
                    if (!used) { body.push_back(c); add(body); break; }
                }
            }
        }

        // A6. 三带二
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] >= 3) {
                auto body = take(r, 3);
                // 找一对 kicker（不能是 body 里的 3 张）
                bool found = false;
                for (int r2 = 0; r2 <= 14 && !found; ++r2) {
                    if (r2 == r ? freq[r2] >= 5 : freq[r2] >= 2) {
                        auto kick = take(r2, 2);
                        auto v = body;
                        v.insert(v.end(), kick.begin(), kick.end());
                        add(v);
                        found = true;
                    }
                }
            }
        }

        // A7. 四带二 (6张: 四张 + 两个单张)
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] >= 4) {
                auto body = take(r, 4);
                std::vector<uint8_t> v = body;
                for (uint8_t c : hand) {
                    bool used = false;
                    for (auto b : v) if (c == b) { used = true; break; }
                    if (!used) {
                        v.push_back(c);
                        if (v.size() == 6) { add(v); break; }
                    }
                }
            }
        }

        // A8. 四带两对 (8张: 四张 + 两个对子)
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] >= 4) {
                auto body = take(r, 4);
                std::vector<uint8_t> v = body;
                int pair_cnt = 0;
                for (int r2 = 0; r2 <= 14 && pair_cnt < 2; ++r2) {
                    if (r2 == r) continue; // 主体已占 4 张，同点数无剩余
                    if (freq[r2] >= 2) {
                        auto p = take(r2, 2);
                        v.insert(v.end(), p.begin(), p.end());
                        pair_cnt++;
                    }
                }
                if (pair_cnt == 2) add(std::move(v));
            }
        }

        // A9. 飞机 (K>=2 组连续三张，可带纯/单翅/对翅)
        for (int K = 2; K <= 12; ++K) {
            for (int lo = 0; lo + K <= 12; ++lo) {
                bool ok = true;
                for (int r = lo; r < lo + K; ++r)
                    if (freq[r] < 3) { ok = false; break; }
                if (!ok) continue;
                // 纯飞机
                {
                    std::vector<uint8_t> v;
                    for (int r = lo; r < lo + K; ++r) {
                        auto t = take(r, 3);
                        v.insert(v.end(), t.begin(), t.end());
                    }
                    add(std::move(v));
                }
                // 带单翅
                {
                    std::vector<uint8_t> v;
                    for (int r = lo; r < lo + K; ++r) {
                        auto t = take(r, 3);
                        v.insert(v.end(), t.begin(), t.end());
                    }
                    for (uint8_t c : hand) {
                        bool used = false;
                        for (auto b : v) if (c == b) { used = true; break; }
                        if (!used) {
                            v.push_back(c);
                            if ((int)v.size() == K * 4) { add(v); break; }
                        }
                    }
                }
                // 带对翅
                {
                    std::vector<uint8_t> body;
                    for (int r = lo; r < lo + K; ++r) {
                        auto t = take(r, 3);
                        body.insert(body.end(), t.begin(), t.end());
                    }
                    std::vector<uint8_t> v = body;
                    int wings = 0;
                    for (int r = 0; r <= 14 && wings < K; ++r) {
                        bool isBody = (r >= lo && r < lo + K);
                        int avail = isBody ? freq[r] - 3 : freq[r];
                        if (avail >= 2) {
                            auto p = take(r, 2);
                            v.insert(v.end(), p.begin(), p.end());
                            ++wings;
                        }
                    }
                    if (wings == K) add(std::move(v));
                }
            }
        }

        // A9. 炸弹 (3 张或 4 张同点数)
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] == 3) add(take(r, 3));
            if (freq[r] == 4) add(take(r, 4));
        }

        // A9. 王炸
        if (freq[13] >= 1 && freq[14] >= 1)
            add({take(13, 1)[0], take(14, 1)[0]});
    }
    // ===========================================================
    // 场景 B：last_cards 是炸弹
    // ===========================================================
    else if (target_type == CardType::BOMB) {
        // B1. 更大炸弹
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] >= 4) {
                if (4 > target_size || (4 == target_size && r > target_rank))
                    add(take(r, 4));
            }
            if (freq[r] >= 3) {
                if (3 > target_size || (3 == target_size && r > target_rank))
                    add(take(r, 3));
            }
        }
        // B2. 王炸
        if (freq[13] >= 1 && freq[14] >= 1)
            add({take(13, 1)[0], take(14, 1)[0]});
    }
    // ===========================================================
    // 场景 C：last_cards 是普通牌型
    // ===========================================================
    else {
        // C1. 同类型更大
        switch (target_type) {
        case CardType::SINGLE:
            for (int r = target_rank + 1; r <= 14; ++r)
                if (freq[r] >= 1) add(take(r, 1));
            break;

        case CardType::PAIR:
            for (int r = target_rank + 1; r <= 14; ++r)
                if (freq[r] >= 2) add(take(r, 2));
            break;

        case CardType::STRAIGHT: {
            // 同起点 (target_rank)、更长
            for (int longer_len = target_size + 1; longer_len <= 12; ++longer_len) {
                int lo = target_rank;
                if (lo + longer_len > 12) continue;
                bool ok = true;
                for (int r = lo; r < lo + longer_len; ++r)
                    if (freq[r] < 1) { ok = false; break; }
                if (ok) {
                    std::vector<uint8_t> v;
                    for (int r = lo; r < lo + longer_len; ++r) {
                        auto t = take(r, 1);
                        v.insert(v.end(), t.begin(), t.end());
                    }
                    add(std::move(v));
                }
            }
            // 起点更大、长度至少等于 target_size (拦短压长)
            for (int len = target_size; len <= 12; ++len) {
                for (int lo = target_rank + 1; lo + len <= 12; ++lo) {
                    bool ok = true;
                    for (int r = lo; r < lo + len; ++r)
                        if (freq[r] < 1) { ok = false; break; }
                    if (ok) {
                        std::vector<uint8_t> v;
                        for (int r = lo; r < lo + len; ++r) {
                            auto t = take(r, 1);
                            v.insert(v.end(), t.begin(), t.end());
                        }
                        add(std::move(v));
                    }
                }
            }
            break;
        }

        case CardType::CONSECUTIVE_PAIRS: {
            int pairs = target_size / 2;
            int target_lo = target_rank - pairs + 1;
            for (int lo = target_lo + 1; lo + pairs <= 12; ++lo) {
                bool ok = true;
                for (int r = lo; r < lo + pairs; ++r)
                    if (freq[r] < 2) { ok = false; break; }
                if (ok) {
                    std::vector<uint8_t> v;
                    for (int r = lo; r < lo + pairs; ++r) {
                        auto t = take(r, 2);
                        v.insert(v.end(), t.begin(), t.end());
                    }
                    add(std::move(v));
                }
            }
            break;
        }

        case CardType::TRIPLE_ONE:
            for (int r = target_rank + 1; r <= 12; ++r) {
                if (freq[r] >= 3) {
                    auto body = take(r, 3);
                    for (uint8_t c : hand) {
                        bool used = false;
                        for (auto b : body) if (c == b) { used = true; break; }
                        if (!used) { body.push_back(c); add(body); break; }
                    }
                }
            }
            break;

        case CardType::TRIPLE_TWO:
            for (int r = target_rank + 1; r <= 12; ++r) {
                if (freq[r] >= 3) {
                    auto body = take(r, 3);
                    bool found = false;
                    for (int r2 = 0; r2 <= 14 && !found; ++r2) {
                        if (r2 == r ? freq[r2] >= 5 : freq[r2] >= 2) {
                            auto kick = take(r2, 2);
                            auto v = body;
                            v.insert(v.end(), kick.begin(), kick.end());
                            add(v);
                            found = true;
                        }
                    }
                }
            }
            break;

        case CardType::QUAD_TWO:
            for (int r = target_rank + 1; r <= 12; ++r) {
                if (freq[r] >= 4) {
                    auto body = take(r, 4);
                    std::vector<uint8_t> v = body;
                    for (uint8_t c : hand) {
                        bool used = false;
                        for (auto b : v) if (c == b) { used = true; break; }
                        if (!used) {
                            v.push_back(c);
                            if (v.size() == 6) { add(v); break; }
                        }
                    }
                }
            }
            break;

        case CardType::QUAD_TWO_PAIRS: {
            auto lastBodies = GetQuadBodyRanks(last_cards);
            for (int lb : lastBodies) {
                for (int r = lb + 1; r <= 12; ++r) {
                    if (freq[r] < 4) continue;
                    auto body = take(r, 4);
                    std::vector<uint8_t> v = body;
                    int pair_cnt = 0;
                    for (int r2 = 0; r2 <= 14 && pair_cnt < 2; ++r2) {
                        if (r2 == r) continue; // 主体已占 4 张，同点数无剩余
                        if (freq[r2] >= 2) {
                            auto p = take(r2, 2);
                            v.insert(v.end(), p.begin(), p.end());
                            pair_cnt++;
                        }
                    }
                    if (pair_cnt == 2) add(std::move(v));
                }
            }
            break;
        }

        case CardType::AIRPLANE: {
            auto lastInterps = GetAirplaneInterpretations(last_cards);
            for (auto& interp : lastInterps) {
                int K = interp.K;
                int wingKind = interp.wingKind;
                int lLo = interp.bodyLo;
                for (int lo = lLo + 1; lo + K <= 12; ++lo) {
                    bool ok = true;
                    for (int r = lo; r < lo + K; ++r)
                        if (freq[r] < 3) { ok = false; break; }
                    if (!ok) continue;
                    if (wingKind == 3) {
                        std::vector<uint8_t> v;
                        for (int r = lo; r < lo + K; ++r) {
                            auto t = take(r, 3);
                            v.insert(v.end(), t.begin(), t.end());
                        }
                        add(std::move(v));
                    }
                    else if (wingKind == 4) {
                        std::vector<uint8_t> v;
                        for (int r = lo; r < lo + K; ++r) {
                            auto t = take(r, 3);
                            v.insert(v.end(), t.begin(), t.end());
                        }
                        for (uint8_t c : hand) {
                            bool used = false;
                            for (auto b : v) if (c == b) { used = true; break; }
                            if (!used) {
                                v.push_back(c);
                                if ((int)v.size() == K * 4) { add(v); break; }
                            }
                        }
                    }
                    else {
                        std::vector<uint8_t> body;
                        for (int r = lo; r < lo + K; ++r) {
                            auto t = take(r, 3);
                            body.insert(body.end(), t.begin(), t.end());
                        }
                        std::vector<uint8_t> v = body;
                        int wings = 0;
                        for (int r = 0; r <= 14 && wings < K; ++r) {
                            bool isBody = (r >= lo && r < lo + K);
                            int avail = isBody ? freq[r] - 3 : freq[r];
                            if (avail >= 2) {
                                auto p = take(r, 2);
                                v.insert(v.end(), p.begin(), p.end());
                                ++wings;
                            }
                        }
                        if (wings == K) add(std::move(v));
                    }
                }
            }
            break;
        }

        default:
            break;
        }

        // C2. 炸弹 (所有炸弹 + 王炸都可压普通牌型)
        for (int r = 0; r <= 12; ++r) {
            if (freq[r] == 3) add(take(r, 3));
            if (freq[r] == 4) add(take(r, 4));
        }
        if (freq[13] >= 1 && freq[14] >= 1)
            add({take(13, 1)[0], take(14, 1)[0]});
    }

    // ---- 排序：按牌力从小到大 ----
    std::sort(result.begin(), result.end(),
        [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
            CardType ta = EvaluateType(a);
            CardType tb = EvaluateType(b);
            int oa = TypeOrder(ta), ob = TypeOrder(tb);
            if (oa != ob) return oa < ob;
            return BodyRank(a, ta) < BodyRank(b, tb);
        });

    return result;
}
