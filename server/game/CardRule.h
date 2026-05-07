#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>  // std::sort, std::max_element

// ===================================================================
// 牌型枚举 —— 五人斗地主合法出牌类型
// ===================================================================
enum class CardType : uint8_t {
    INVALID,            // 非法牌型
    SINGLE,             // 单张
    PAIR,               // 对子 (2 张同点数)
    STRAIGHT,           // 顺子 (>=3 张连续点数, 不含2和王)
    CONSECUTIVE_PAIRS,  // 连对 (>=2 对连续点数, 不含2和王, 如 33 44 55)
    TRIPLE_ONE,         // 三带一 (3+1, 4 张)
    TRIPLE_TWO,         // 三带二 (3+2, 5 张)
    QUAD_TWO,           // 四带二 (4+2单, 6 张)
    QUAD_TWO_PAIRS,     // 四带两对 (4+2对, 8 张)
    AIRPLANE,           // 飞机 (K>=2组连续三张, 可带单翅/对翅)
    BOMB,               // 炸弹 (3 张或 4 张同点数)
    ROCKET,             // 王炸 (小王 53 + 大王 56)
};

// ===================================================================
// CardRule —— 纯静态数学裁判，将 0-56 的原生卡牌编码映射为物理牌力
//
// 卡牌编码规则：
//   ┌──────────┬─────────────────────────────────────┐
//   │ 编码范围  │ 含义                                │
//   ├──────────┼─────────────────────────────────────┤
//   │ 0-51     │ 普通牌 (4 花色 × 13 点数)            │
//   │          │ val / 4 = 逻辑点数 (0=3,1=4,...,12=2) │
//   │          │ val % 4 = 花色 (0=方,1=梅,2=红,3=黑) │
//   │ 52       │ 保留 (未使用)                        │
//   │ 53       │ 小王 (53/4=13, 点数 13)              │
//   │ 54-55    │ 保留 (未使用)                        │
//   │ 56       │ 大王 (56/4=14, 点数 14)              │
//   └──────────┴─────────────────────────────────────┘
//
// 设计要点：大王 56 > 小王 53，直接数值比较即可分大小，无需额外判断。
//          GetRank() 返回 14 > 13，逻辑点数比较同样成立。
//
// 压制规则：
//   王炸 > 炸弹 > 普通牌型
//   顺子：最小点数低的一方绝不能压高的一方；同起点比长度；起点更高须长度不短
//   飞机：同组数 (K) 且同翅型 (纯/单翅/对翅)，比主体最小点数
//   其他普通牌型：必须同类型且同张数，主体点数更大
//   炸弹之间：先比张数（4 > 3），同张数比点数
// ===================================================================
class CardRule {
public:
    // ---- 卡牌原子操作 ----

    // 提取逻辑点数 (0-14)。同点数 = 同大小，花色在本游戏中无意义。
    // 例：方块3=0/4=0, 大王=56/4=14
    static inline int GetRank(uint8_t card) { return card / 4; }

    // 提取花色 (0-3)。仅用于首叫权判定 (找方块3/方块4)。
    // 0=方块, 1=梅花, 2=红桃, 3=黑桃
    static inline int GetSuit(uint8_t card) { return card % 4; }

    // 判断是否大小王
    static inline bool IsSmallJoker(uint8_t card) { return card == 53; }
    static inline bool IsBigJoker(uint8_t card)  { return card == 56; }
    static inline bool IsJoker(uint8_t card)      { return card == 53 || card == 56; }

    // 判断是否为普通点数牌 (0-51)
    static inline bool IsNormalCard(uint8_t card) { return card <= 51; }

    // ---- 牌型判定 ----

    // 输入一手牌，返回它的牌型。
    // 内部按点数分组统计后走判定链：王炸 → 炸弹 → 单张/对子/顺子/连对/三带/四带
    static CardType EvaluateType(const std::vector<uint8_t>& cards);

    // ---- 压制判定 ----

    // play_cards 能否压过 last_cards？
    // - 王炸无敌，通吃一切
    // - 炸弹只能被更大炸弹/王炸压制（普通牌型不能压炸弹）
    // - 普通牌型：必须同类型且同张数，最大点数更大
    // 返回 true 表示可以出，false 表示压不过
    static bool CanBeat(const std::vector<uint8_t>& play_cards,
                        const std::vector<uint8_t>& last_cards);

    // ---- 排序工具 ----

    // 按点数升序排列手牌，同点数按花色排列，王放在最后
    static void SortHand(std::vector<uint8_t>& hand);

    // ---- 提示生成 ----
    //
    // 核心数据结构：int freq[15] —— 点数 0(3) ~ 14(大王) 各有多少张
    //
    // 搜索策略（四分支）：
    //   1. last_cards 是王炸 → 返回空（没人压得住）
    //   2. last_cards 是炸弹 → 搜更大的炸弹 + 王炸
    //   3. last_cards 是普通牌型 → 搜同类型更大点数 + 搜所有炸弹/王炸
    //      （顺子特殊：起点相同则更长可压；起点更大则任意长度可压）
    //      （飞机特殊：同组数 K 且主体最小点数更大可压，翅型不拘）
    //   4. last_cards 为空（自由出牌）→ 枚举所有可能牌型（从小到大）
    //
    // 各牌型搜索方法：
    //   - 单张/对子：线性扫描 freq，找 rank > target 且 count >= 需要张数  O(15)
    //   - 顺子：    滑动窗口，长度 L 的窗口内所有 rank 都有 count >= 1  O(15)
    //               rank 范围 0~11（3~A，不含2和王）
    //   - 连对：    滑动窗口，长度 K 的窗口内所有 rank 都有 count >= 2  O(15)
    //               rank 范围 0~11（3~A，不含2和王）
    //   - 飞机：    搜 K>=2 组连续 triple，再拼纯/单翅/对翅         O(15²)
    //   - 三带一/二：先找 rank > target 的 triple，再从剩余牌贪心找 kicker  O(15²)
    //   - 四带二：  找 rank > target 的 quad，从剩余牌贪心找两个 kicker    O(15²)
    //   - 炸弹：    扫描 freq 找 count >= 3 或 count >= 4 的 rank          O(15)
    //
    // 排序策略：按牌力从小到大排列（单张 → 对子 → 顺子 → 连对 → 三带一 → 三带二 → 四带二 → 飞机 → 炸弹 → 王炸）
    // AI 决策才需要评估手牌结构破坏度，提示功能只做简单排序

    // 返回所有能压过 last_cards 的合法出牌组合，按牌力从小到大排列
    // last_cards 为空时表示自由出牌，返回所有可能的合法牌型组合
    static std::vector<std::vector<uint8_t>> GetHints(
        const std::vector<uint8_t>& hand,
        const std::vector<uint8_t>& last_cards);
};

// ===================================================================
// 点数定义常量 (GetRank 的返回值)
// ===================================================================
namespace CardRank {
    constexpr int THREE  = 0;   // 3
    constexpr int FOUR   = 1;   // 4
    constexpr int FIVE   = 2;   // 5
    constexpr int SIX    = 3;   // 6
    constexpr int SEVEN  = 4;   // 7
    constexpr int EIGHT  = 5;   // 8
    constexpr int NINE   = 6;   // 9
    constexpr int TEN    = 7;   // 10
    constexpr int JACK   = 8;   // J
    constexpr int QUEEN  = 9;   // Q
    constexpr int KING   = 10;  // K
    constexpr int ACE    = 11;  // A
    constexpr int TWO    = 12;  // 2
    constexpr int SMALL_JOKER = 13;  // 小王
    constexpr int BIG_JOKER   = 14;  // 大王
}

// ===================================================================
// 特殊卡牌编码常量
// ===================================================================
namespace CardCode {
    constexpr uint8_t DIAMOND_3   = 0;   // 方块3 (首叫权判定用)
    constexpr uint8_t DIAMOND_4   = 4;   // 方块4 (首叫权判定用)
    constexpr uint8_t SMALL_JOKER = 53;  // 小王
    constexpr uint8_t BIG_JOKER   = 56;  // 大王
}
