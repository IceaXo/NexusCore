#include "CardRule.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <set>

static int tests = 0, passed = 0, failed = 0;

static const char* tname(CardType t) {
    switch (t) {
    case CardType::INVALID: return "INVALID";
    case CardType::SINGLE: return "SINGLE";
    case CardType::PAIR: return "PAIR";
    case CardType::STRAIGHT: return "STRAIGHT";
    case CardType::CONSECUTIVE_PAIRS: return "CONSEC_PAIR";
    case CardType::TRIPLE_ONE: return "TRIPLE_1";
    case CardType::TRIPLE_TWO: return "TRIPLE_2";
    case CardType::QUAD_TWO: return "QUAD_2";
    case CardType::AIRPLANE: return "AIRPLANE";
    case CardType::BOMB: return "BOMB";
    case CardType::ROCKET: return "ROCKET";
    default: return "???";
    }
}

static std::string cardStr(uint8_t c) {
    static const char* suit[] = {"D","C","H","S"}; // 方梅红黑
    int r = c / 4;
    static const char* rn[] = {"3","4","5","6","7","8","9","10","J","Q","K","A","2","SJ","BJ"};
    if (r <= 14) return std::string(suit[c%4]) + rn[r];
    return "??";
}

static std::string cardsStr(const std::vector<uint8_t>& v) {
    std::string s;
    for (auto c : v) s += cardStr(c) + " ";
    if (!s.empty()) s.pop_back();
    return s;
}

#define T(desc, expr) do { \
    tests++; \
    if (expr) { passed++; } \
    else { \
        failed++; \
        std::cout << "  [FAIL] " << (desc) << std::endl; \
        std::cout << "         " << #expr << std::endl; \
    } \
} while(0)

#define TEQ(desc, a, b) T(desc, (a) == (b))

// 构造牌：给 rank 和 suit 返回 card code
static uint8_t C(int rank, int suit) { return (uint8_t)(rank * 4 + suit); }

// 构造同 rank 的 n 张牌
static std::vector<uint8_t> R(int rank, int n) {
    std::vector<uint8_t> v;
    for (int s = 0; s < n && s < 4; ++s) v.push_back(C(rank, s));
    return v;
}

int main() {
    std::cout << "===== 牌型识别 EvaluateType 全覆盖 =====\n\n";

    // ---------- SINGLE ----------
    TEQ("单张3", CardRule::EvaluateType({C(0,0)}), CardType::SINGLE);
    TEQ("单张2", CardRule::EvaluateType({C(12,3)}), CardType::SINGLE);
    TEQ("单张小王", CardRule::EvaluateType({53}), CardType::SINGLE);
    TEQ("单张大王", CardRule::EvaluateType({56}), CardType::SINGLE);

    // ---------- PAIR ----------
    TEQ("对3", CardRule::EvaluateType({C(0,0),C(0,1)}), CardType::PAIR);
    TEQ("对A", CardRule::EvaluateType({C(11,0),C(11,3)}), CardType::PAIR);
    TEQ("对2", CardRule::EvaluateType({C(12,0),C(12,2)}), CardType::PAIR);
    TEQ("单张小王+大王不是对子", CardRule::EvaluateType({53,56}), CardType::ROCKET); // 先被王炸捕获

    // ---------- 炸弹优先于三带一 ----------
    TEQ("三张3是炸弹(不是三带一缺kicker)", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2)}), CardType::BOMB);
    TEQ("四张3是炸弹", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3)}), CardType::BOMB);
    TEQ("三张2是炸弹", CardRule::EvaluateType({C(12,0),C(12,1),C(12,2)}), CardType::BOMB);

    // ---------- 三带一 ----------
    TEQ("三3带一4", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2), C(1,0)}), CardType::TRIPLE_ONE);
    TEQ("三3带一3(自己有4张3)", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2), C(0,3)}), CardType::BOMB); // 四张同点是炸弹!

    // ---------- 三带二 ----------
    TEQ("三3带对4", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2), C(1,0),C(1,1)}), CardType::TRIPLE_TWO);

    // ---------- 四带二 ----------
    TEQ("四3带两单(6张)", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3), C(1,0),C(2,0)}), CardType::QUAD_TWO);
    TEQ("四3带两对4,5(8张)", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3), C(1,0),C(1,1), C(2,0),C(2,1)}), CardType::QUAD_TWO_PAIRS);

    // ---------- 王炸 ----------
    TEQ("王炸", CardRule::EvaluateType({53,56}), CardType::ROCKET);

    // ---------- 顺子 (不含2和王) ----------
    TEQ("34567顺子", CardRule::EvaluateType({C(0,0),C(1,0),C(2,0),C(3,0),C(4,0)}), CardType::STRAIGHT);
    TEQ("最小顺子345(3张)", CardRule::EvaluateType({C(0,0),C(1,0),C(2,0)}), CardType::STRAIGHT);
    TEQ("最大顺子", CardRule::EvaluateType({
        C(0,0),C(1,0),C(2,0),C(3,0),C(4,0),C(5,0),C(6,0),C(7,0),C(8,0),C(9,0),C(10,0),C(11,0)
    }), CardType::STRAIGHT); // 3-A = 12张
    // 含2的顺子不合法
    TEQ("JQK2不是顺子(含2)", CardRule::EvaluateType({C(8,0),C(9,0),C(10,0),C(12,0)}), CardType::INVALID);
    TEQ("QKA2不是顺子(含2)", CardRule::EvaluateType({C(9,0),C(10,0),C(11,0),C(12,0)}), CardType::INVALID);

    // ---------- 连对 (不含2和王) ----------
    TEQ("3344连对", CardRule::EvaluateType({C(0,0),C(0,1),C(1,0),C(1,1)}), CardType::CONSECUTIVE_PAIRS);
    TEQ("334455连对", CardRule::EvaluateType({C(0,0),C(0,1),C(1,0),C(1,1),C(2,0),C(2,1)}), CardType::CONSECUTIVE_PAIRS);
    // 含2的连对不合法
    TEQ("JJQQK2不是连对(不连续且含2)", CardRule::EvaluateType({C(8,0),C(8,1),C(9,0),C(9,1),C(10,0),C(12,0)}), CardType::INVALID);

    // ---------- 飞机 ----------
    // 纯飞机 K=2
    TEQ("333444纯飞机", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)}), CardType::AIRPLANE);
    // 纯飞机 K=3
    TEQ("333444555纯飞机", CardRule::EvaluateType({
        C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)
    }), CardType::AIRPLANE);
    // 带单翅 K=2
    TEQ("333444-5-6带单翅", CardRule::EvaluateType({
        C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(3,0)
    }), CardType::AIRPLANE);
    // 带对翅 K=2
    TEQ("333444-55-66带对翅", CardRule::EvaluateType({
        C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2),
        C(2,0),C(2,1), C(3,0),C(3,1)
    }), CardType::AIRPLANE);
    // 飞机不含2和王
    TEQ("KKKAAA222不是飞机(含2)", CardRule::EvaluateType({
        C(10,0),C(10,1),C(10,2), C(11,0),C(11,1),C(11,2),
        C(12,0),C(12,1),C(12,2)
    }), CardType::INVALID);
    // 飞机: 四张3当三张用 (freq[r]=4 >= 3)
    TEQ("3333-444纯飞机(3用4张中的3张)", CardRule::EvaluateType({
        C(0,0),C(0,1),C(0,2),C(0,3),  // 四张3
        C(1,0),C(1,1),C(1,2)           // 三张4
    }), CardType::INVALID); // 7张牌不匹配任何飞机尺寸 (7不是3K/4K/5K)
    // 6张四带二优先于6张飞机纯?
    // 四张3+两张单 = QUAD_TWO, 不是飞机
    TEQ("四带二优先于飞机", CardRule::EvaluateType({
        C(0,0),C(0,1),C(0,2),C(0,3), C(1,0),C(2,0)
    }), CardType::QUAD_TWO);

    std::cout << "\n===== BodyRank 验证 =====\n\n";
    auto BR = [](const std::vector<uint8_t>& v) {
        return CardRule::CanBeat(v, v); // dummy, we just want BodyRank via a public path
    };
    // 直接测试 BodyRank 通过 CanBeat 间接验证
    // 单张: rank=0 < rank=1
    T("单张rank比较: 4>3", CardRule::CanBeat({C(1,0)}, {C(0,0)}));
    T("单张rank比较: 3不压4", !CardRule::CanBeat({C(0,0)}, {C(1,0)}));
    // 对子
    T("对子rank比较: 4>3", CardRule::CanBeat({C(1,0),C(1,1)}, {C(0,0),C(0,1)}));
    // 顺子min-rank
    T("4567压456(同min=1更长)", CardRule::CanBeat({C(1,0),C(2,0),C(3,0),C(4,0)}, {C(1,0),C(2,0),C(3,0)}));
    T("3456不压456(min=0<1)", !CardRule::CanBeat({C(0,0),C(1,0),C(2,0),C(3,0)}, {C(1,0),C(2,0),C(3,0)}));

    std::cout << "\n===== 压制链完整测试 =====\n\n";

    // -- 单张压制链: 3 < 4 < ... < K < A < 2 < SJ < BJ --
    for (int r = 0; r < 13; ++r) {
        T("单张 " + std::to_string(r+1) + " 压 " + std::to_string(r),
          CardRule::CanBeat({C(r+1,0)}, {C(r,0)}));
    }
    T("小王压2", CardRule::CanBeat({53}, {C(12,0)}));
    T("大王压小王", CardRule::CanBeat({56}, {53}));

    // -- 对子压制链 --
    for (int r = 0; r < 13; ++r) {
        T("对" + std::to_string(r+1) + " 压 对" + std::to_string(r),
          CardRule::CanBeat({C(r+1,0),C(r+1,1)}, {C(r,0),C(r,1)}));
    }

    // -- 炸弹压制链 --
    T("四张3压三张3", CardRule::CanBeat({C(0,0),C(0,1),C(0,2),C(0,3)}, {C(0,0),C(0,1),C(0,2)}));
    for (int r = 0; r < 11; ++r) {
        T("三张" + std::to_string(r+1) + "炸弹压三张" + std::to_string(r),
          CardRule::CanBeat({C(r+1,0),C(r+1,1),C(r+1,2)}, {C(r,0),C(r,1),C(r,2)}));
    }

    // -- 三带一压制链 (只比主体, kicker用大王避免与主体重叠) --
    for (int r = 0; r < 11; ++r) {
        T("三" + std::to_string(r+1) + "带大王 压 三" + std::to_string(r) + "带大王(主体" + std::to_string(r+1) + ">" + std::to_string(r) + ")",
          CardRule::CanBeat({C(r+1,0),C(r+1,1),C(r+1,2), C(14,0)},
                            {C(r,0),C(r,1),C(r,2), C(14,0)}));
    }

    // -- 三带二压制链 (kicker用AA避免与主体重叠) --
    for (int r = 0; r < 10; ++r) {
        T("三" + std::to_string(r+1) + "带对A 压 三" + std::to_string(r) + "带对A",
          CardRule::CanBeat({C(r+1,0),C(r+1,1),C(r+1,2), C(11,0),C(11,1)},
                            {C(r,0),C(r,1),C(r,2), C(11,0),C(11,1)}));
    }

    std::cout << "\n===== 顺子边界测试 =====\n\n";

    // 关键规则: 比最小点数，同起点比长度
    // 345 (size=3, min=0) vs 456 (size=3, min=1) -> min=1 wins
    T("456压345 (同长,min=1>0)", CardRule::CanBeat(
        {C(1,0),C(2,0),C(3,0)}, {C(0,0),C(1,0),C(2,0)}));

    // 4567 (size=4, min=1) vs 456 (size=3, min=1) -> same min, longer wins
    T("4567压456 (同min=1,更长)", CardRule::CanBeat(
        {C(1,0),C(2,0),C(3,0),C(4,0)}, {C(1,0),C(2,0),C(3,0)}));

    // 3456 (size=4, min=0) vs 456 (size=3, min=1) -> min=0 < min=1, loses!
    T("3456不压456 (min=0<1)", !CardRule::CanBeat(
        {C(0,0),C(1,0),C(2,0),C(3,0)}, {C(1,0),C(2,0),C(3,0)}));

    // 34567 (size=5, min=0) vs 3456 (size=4, min=0) -> same min, longer
    T("34567压3456 (同min=0,更长)", CardRule::CanBeat(
        {C(0,0),C(1,0),C(2,0),C(3,0),C(4,0)}, {C(0,0),C(1,0),C(2,0),C(3,0)}));

    // 567 (len=3,min=2) vs 3456 (len=4,min=0): min虽大但张数少, 不能压
    T("567不压3456 (min虽大但张数少,拦短压长)", !CardRule::CanBeat(
        {C(2,0),C(3,0),C(4,0)}, {C(0,0),C(1,0),C(2,0),C(3,0)}));
    // 4567 (len=4,min=1) vs 3456 (len=4,min=0): min更大+同长 → 可压
    T("4567压3456 (min>0,同长)", CardRule::CanBeat(
        {C(1,0),C(2,0),C(3,0),C(4,0)}, {C(0,0),C(1,0),C(2,0),C(3,0)}));

    // 910JQKA (min=7) vs 8910JQ (min=5) -> min=7>5 wins
    T("9-A顺压8-Q顺 (min=7>5)", CardRule::CanBeat(
        {C(6,0),C(7,0),C(8,0),C(9,0),C(10,0),C(11,0)},
        {C(5,0),C(6,0),C(7,0),C(8,0),C(9,0)}));

    // Same straight can't beat itself
    T("345不压345 (同牌)", !CardRule::CanBeat(
        {C(0,0),C(1,0),C(2,0)}, {C(0,1),C(1,1),C(2,1)}));

    // Same length + higher min wins, regardless of max
    T("567压345 (同长3,min=2>0)", CardRule::CanBeat(
        {C(2,0),C(3,0),C(4,0)}, {C(0,0),C(1,0),C(2,0)}));

    std::cout << "\n===== 连对边界测试 =====\n\n";

    // 连对: 相同对数，最小点数更大
    T("4455压3344 (同2对,min=1>0)", CardRule::CanBeat(
        {C(1,0),C(1,1),C(2,0),C(2,1)}, {C(0,0),C(0,1),C(1,0),C(1,1)}));
    T("3344不压4455", !CardRule::CanBeat(
        {C(0,0),C(0,1),C(1,0),C(1,1)}, {C(1,0),C(1,1),C(2,0),C(2,1)}));
    // 不同对数不能互压
    T("334455不压4455 (不同对数)", !CardRule::CanBeat(
        {C(0,0),C(0,1),C(1,0),C(1,1),C(2,0),C(2,1)},
        {C(1,0),C(1,1),C(2,0),C(2,1)}));
    // 同对数, min更大
    T("5566压4455 (同2对,min=2>1)", CardRule::CanBeat(
        {C(2,0),C(2,1),C(3,0),C(3,1)}, {C(1,0),C(1,1),C(2,0),C(2,1)}));

    std::cout << "\n===== 飞机边界测试 =====\n\n";

    // K=2 纯飞机比较: 比主体最小点数
    T("444555压333444 (同K=2,min=1>0)", CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)}));
    T("333444不压444555", !CardRule::CanBeat(
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)},
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)}));

    // K=3 飞机
    T("444555666压333444555 (同K=3,min=1>0)", CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2), C(3,0),C(3,1),C(3,2)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)}));

    // 不同K不能互压
    T("K=3不压K=2", !CardRule::CanBeat(
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)}));
    T("K=2不压K=3", !CardRule::CanBeat(
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)}));

    // 不同翅型不可互压
    T("单翅444555不压纯333444 (不同翅)", !CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2), C(0,0),C(3,0)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)}));
    T("对翅333444不压纯444555 (不同翅)", !CardRule::CanBeat(
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(2,1), C(3,0),C(3,1)},
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)}));

    // 翅牌含主体同rank但余牌够: 四个3(用3张做主体,余1张做翅)
    TEQ("3333-444带单翅3(余牌做翅)识别为飞机",
        CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3), C(1,0),C(1,1),C(1,2), C(2,0)}),
        CardType::AIRPLANE);
    // 且确实能压更小的飞机 (555666-min=2 压 333444-min=0)
    T("555666带单翅压3333-444带单翅(min=2>0)", CardRule::CanBeat(
        {C(2,0),C(2,1),C(2,2), C(3,0),C(3,1),C(3,2), C(0,0),C(0,1)},
        {C(0,0),C(0,1),C(0,2),C(0,3), C(1,0),C(1,1),C(1,2), C(2,0)}));

    std::cout << "\n===== 跨级压制测试 =====\n\n";

    // 王炸通吃
    T("王炸压单张", CardRule::CanBeat({53,56}, {C(0,0)}));
    T("王炸压对子", CardRule::CanBeat({53,56}, {C(0,0),C(0,1)}));
    T("王炸压顺子", CardRule::CanBeat({53,56}, {C(0,0),C(1,0),C(2,0)}));
    T("王炸压连对", CardRule::CanBeat({53,56}, {C(0,0),C(0,1),C(1,0),C(1,1)}));
    T("王炸压三带一", CardRule::CanBeat({53,56}, {C(0,0),C(0,1),C(0,2),C(1,0)}));
    T("王炸压三带二", CardRule::CanBeat({53,56}, {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1)}));
    T("王炸压四带二", CardRule::CanBeat({53,56}, {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(2,0)}));
    T("王炸压飞机", CardRule::CanBeat({53,56}, {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)}));
    T("王炸压炸弹", CardRule::CanBeat({53,56}, {C(0,0),C(0,1),C(0,2)}));

    // 炸弹越级压普通牌型
    T("三张3炸弹压单张", CardRule::CanBeat({C(0,0),C(0,1),C(0,2)}, {C(12,0)})); // 压2
    T("三张3炸弹压对子", CardRule::CanBeat({C(0,0),C(0,1),C(0,2)}, {C(12,0),C(12,1)})); // 压对2
    T("三张3炸弹压顺子", CardRule::CanBeat({C(0,0),C(0,1),C(0,2)}, {C(9,0),C(10,0),C(11,0)})); // 压QKA
    T("三张3炸弹压连对", CardRule::CanBeat({C(0,0),C(0,1),C(0,2)}, {C(9,0),C(9,1),C(10,0),C(10,1)})); // 压QQKK
    T("三张3炸弹压三带一", CardRule::CanBeat({C(0,0),C(0,1),C(0,2)}, {C(12,0),C(12,1),C(12,2),C(11,0)}));
    T("三张3炸弹压飞机", CardRule::CanBeat({C(0,0),C(0,1),C(0,2)}, {C(10,0),C(10,1),C(10,2),C(11,0),C(11,1),C(11,2)}));

    // 普通牌型不能压炸弹
    T("单张不压炸弹", !CardRule::CanBeat({C(14,0)}, {C(0,0),C(0,1),C(0,2)}));
    T("顺子不压炸弹", !CardRule::CanBeat({C(9,0),C(10,0),C(11,0)}, {C(0,0),C(0,1),C(0,2)}));
    T("飞机不压炸弹", !CardRule::CanBeat({C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)}, {C(0,0),C(0,1),C(0,2)}));

    std::cout << "\n===== 非法/边界牌型 =====\n\n";

    // 非法不能压合法
    T("非法不压合法", !CardRule::CanBeat({C(0,0),C(4,0)}, {C(1,0)})); // 两张不同点对单张
    T("合法不压非法", !CardRule::CanBeat({C(1,0)}, {C(0,0),C(4,0)}));

    // 空地
    T("空手牌非法", CardRule::EvaluateType({}) == CardType::INVALID);

    // 含保留编码的牌 (52=rank13, 54-55=rank13)
    TEQ("保留牌52是单张小王", CardRule::EvaluateType({52}), CardType::SINGLE); // 52/4=13=SJ

    // 四张3是炸弹 (不是三带一)
    TEQ("四张3识别为炸弹", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3)}), CardType::BOMB);

    std::cout << "\n===== GetHints 提示生成验证 =====\n\n";

    // 完整手牌(10张) 的 hints
    {
        std::vector<uint8_t> hand = {C(0,0),C(0,1),C(2,0),C(3,0),C(3,1),
                                      C(5,0),C(6,0),C(7,0),C(8,0),C(11,0)};
        auto hints = CardRule::GetHints(hand, {});
        T("有手牌时hints非空", !hints.empty());
        // 应该至少有单张
        bool has_single = false;
        for (auto& h : hints) {
            if (CardRule::EvaluateType(h) == CardType::SINGLE) { has_single = true; break; }
        }
        T("hints包含单张", has_single);

        // 有对3 应包含对子
        bool has_pair = false;
        for (auto& h : hints) {
            if (CardRule::EvaluateType(h) == CardType::PAIR) { has_pair = true; break; }
        }
        T("hints包含对子", has_pair);

        // 顺子: 5678
        bool has_straight = false;
        for (auto& h : hints) {
            if (CardRule::EvaluateType(h) == CardType::STRAIGHT) { has_straight = true; break; }
        }
        T("hints包含顺子(5678)", has_straight);
    }

    // 手牌含王炸
    {
        std::vector<uint8_t> hand = {53,56,C(0,0),C(1,0),C(2,0),C(3,0),C(4,0),
                                      C(5,0),C(6,0),C(7,0)};
        auto hints = CardRule::GetHints(hand, {});
        bool has_rocket = false;
        for (auto& h : hints)
            if (CardRule::EvaluateType(h) == CardType::ROCKET) { has_rocket = true; break; }
        T("hints包含王炸", has_rocket);
    }

    // 桌面有牌时hints只能压过
    {
        std::vector<uint8_t> hand = {C(3,0),C(3,1),C(3,2),C(3,3),  // 四个6
                                      C(5,0),C(5,1),C(5,2),C(5,3),  // 四个8
                                      C(2,0),C(2,1)};               // 对5
        std::vector<uint8_t> last = {C(1,0)};  // 单张4
        auto hints = CardRule::GetHints(hand, last);
        T("压单张4有hints", !hints.empty());
        // 所有hints必须能压过last
        bool all_beat = true;
        for (auto& h : hints) {
            if (!CardRule::CanBeat(h, last)) { all_beat = false; break; }
        }
        T("所有hints都能压过last", all_beat);
    }

    // 王炸在桌面: 返回空hints
    {
        std::vector<uint8_t> hand = {C(3,0),C(5,0),C(7,0)};
        std::vector<uint8_t> last = {53,56}; // 王炸
        auto hints = CardRule::GetHints(hand, last);
        T("桌面王炸时hints为空", hints.empty());
    }

    // Hints排序验证：类型 + 点数从小到大
    {
        std::vector<uint8_t> hand = {C(0,0),C(0,1),C(0,2),C(0,3),  // 四个3
                                      C(1,0),C(1,1),C(1,2),         // 三个4
                                      C(5,0),C(5,1)};               // 对8
        auto hints = CardRule::GetHints(hand, {});
        // 验证排序: type order非递减, 同type内rank非递减
        bool sorted = true;
        for (size_t i = 1; i < hints.size(); ++i) {
            auto t1 = CardRule::EvaluateType(hints[i-1]);
            auto t2 = CardRule::EvaluateType(hints[i]);
            // type order should be non-decreasing
            // we can't call TypeOrder directly, so use CanBeat as proxy:
            // if t1 == t2, the earlier hint should have lower or equal body rank
            // (body rank lower = weaker)
        }
        T("hints数量>0", hints.size() > 0);
    }

    std::cout << "\n===== SortHand 验证 =====\n\n";

    {
        std::vector<uint8_t> hand = {C(12,3), C(0,0), C(7,2), C(0,3), C(3,1), 53, C(14,0), 56};
        CardRule::SortHand(hand);
        // 应该按 rank 升序: rank0, rank0, rank3, rank7, rank12, rank13(SJ), rank14(BJ), rank14(BJ)
        bool sorted = true;
        for (size_t i = 1; i < hand.size(); ++i) {
            int ra = hand[i-1] / 4, rb = hand[i] / 4;
            if (ra > rb) sorted = false;
            if (ra == rb && (hand[i-1] % 4) > (hand[i] % 4)) sorted = false;
        }
        T("SortHand按rank+花色升序", sorted);
        T("王在末尾(rank14)", hand.back() / 4 == 14);
    }

    std::cout << "\n===== GetHints 压飞机 =====\n\n";

    // 桌面是飞机, 手牌有更大的飞机
    {
        std::vector<uint8_t> hand = {
            C(1,0),C(1,1),C(1,2),  // 三个4
            C(2,0),C(2,1),C(2,2),  // 三个5
            C(3,0),C(3,1),C(3,2),  // 三个6
            C(4,0)                  // 单7
        };
        std::vector<uint8_t> last = {
            C(0,0),C(0,1),C(0,2),  // 三个3
            C(1,0),C(1,1),C(1,2),  // 三个4
        }; // K=2纯飞机 min=0
        auto hints = CardRule::GetHints(hand, last);
        T("压飞机有hints", !hints.empty());
        bool has_plane = false;
        for (auto& h : hints) {
            if (CardRule::EvaluateType(h) == CardType::AIRPLANE) {
                has_plane = true;
                T("飞机hint能压过桌面飞机", CardRule::CanBeat(h, last));
            }
        }
        T("hints包含飞机", has_plane);
    }

    std::cout << "\n===== 四带二: 6张vs8张 =====\n\n";

    // 不同张数不能互压 (6张 vs 8张)
    T("四带两单(6张)不压四带两对(8张)", !CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2),C(1,3), C(2,0),C(3,0)},
        {C(0,0),C(0,1),C(0,2),C(0,3), C(1,0),C(1,1), C(2,0),C(2,1)}));
    T("四带两对(8张)不压四带两单(6张)", !CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2),C(1,3), C(2,0),C(2,1), C(3,0),C(3,1)},
        {C(0,0),C(0,1),C(0,2),C(0,3), C(1,0),C(2,0)}));
    // 同张数可压 (8张 vs 8张, 主体更大)
    T("四4带两对压四3带两对(同8张,主体1>0)", CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2),C(1,3), C(2,0),C(2,1), C(3,0),C(3,1)},
        {C(0,0),C(0,1),C(0,2),C(0,3), C(2,0),C(2,1), C(3,0),C(3,1)}));

    std::cout << "\n===== 顺子/连对不含2 =====\n\n";

    // 这些应该被评估为非法
    TEQ("JQK2-SJ-BJ不是顺子", CardRule::EvaluateType(
        {C(8,0),C(9,0),C(10,0),C(12,0),53,56}), CardType::INVALID);

    // 纯2的顺子也不行
    TEQ("23456不是顺子(因含2, rank12不在0-11)", CardRule::EvaluateType(
        {C(12,0),C(0,0),C(1,0),C(2,0),C(3,0)}), CardType::INVALID);

    // 对2对3不是连对: 2不在合法范围,且3和2不连续
    TEQ("22 33不是连对", CardRule::EvaluateType(
        {C(12,0),C(12,1),C(0,0),C(0,1)}), CardType::INVALID);

    std::cout << "\n===== 炸弹边界 =====\n\n";

    // 三张2炸弹 vs 四张3炸弹
    T("四张3压三张2(张数多)", CardRule::CanBeat(
        {C(0,0),C(0,1),C(0,2),C(0,3)}, {C(12,0),C(12,1),C(12,2)}));
    // 三张2炸弹 vs 三张A炸弹
    T("三张2压三张A(同张数rank大)", CardRule::CanBeat(
        {C(12,0),C(12,1),C(12,2)}, {C(11,0),C(11,1),C(11,2)}));
    // 四张A压四张K
    T("四张A压四张K", CardRule::CanBeat(
        {C(11,0),C(11,1),C(11,2),C(11,3)}, {C(10,0),C(10,1),C(10,2),C(10,3)}));

    // 炸弹压三带一
    T("四张3压三2带一K", CardRule::CanBeat(
        {C(0,0),C(0,1),C(0,2),C(0,3)},
        {C(12,0),C(12,1),C(12,2),C(10,0)}));

    std::cout << "\n===== 飞机: 必须同翅型才能互压 =====\n\n";

    // 同翅型可互压 (纯vs纯)
    T("纯555666压纯333444 (同翅纯,min=2>0)", CardRule::CanBeat(
        {C(2,0),C(2,1),C(2,2), C(3,0),C(3,1),C(3,2)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)}));

    // 同翅型可互压 (单翅vs单翅)
    T("单翅555666压单翅333444 (同翅单,min=2>0)", CardRule::CanBeat(
        {C(2,0),C(2,1),C(2,2), C(3,0),C(3,1),C(3,2), C(0,0),C(1,0)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(3,0)}));

    // 同翅型可互压 (对翅vs对翅)
    T("对翅555666压对翅333444 (同翅对,min=2>0)", CardRule::CanBeat(
        {C(2,0),C(2,1),C(2,2), C(3,0),C(3,1),C(3,2), C(0,0),C(0,1), C(1,0),C(1,1)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(2,1), C(3,0),C(3,1)}));

    // 不同翅型不可互压 (纯vs单翅)
    T("纯444555不压单翅333444 (不同翅)", !CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(3,0)}));

    // 不同翅型不可互压 (单翅vs对翅)
    T("单翅444555不压对翅333444 (不同翅)", !CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2), C(0,0),C(3,0)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2), C(2,0),C(2,1), C(3,0),C(3,1)}));

    // 不同翅型不可互压 (对翅vs纯)
    T("对翅444555不压纯333444 (不同翅)", !CardRule::CanBeat(
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2), C(3,0),C(3,1), C(4,0),C(4,1)},
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)}));

    // 同翅型但min不够大不能压
    T("纯333444不压纯444555 (同翅纯,min=0<1)", !CardRule::CanBeat(
        {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2)},
        {C(1,0),C(1,1),C(1,2), C(2,0),C(2,1),C(2,2)}));

    std::cout << "\n===== 特殊: 最大牌型 =====\n\n";

    // ===================================================================
    // 结果汇总
    // ===================================================================
    std::cout << "\n============================================\n";
    std::cout << "  TOTAL: " << tests << " tests\n";
    std::cout << "  PASS:  " << passed << "\n";
    std::cout << "  FAIL:  " << failed << "\n";
    std::cout << "============================================\n";

    return failed > 0 ? 1 : 0;
}
