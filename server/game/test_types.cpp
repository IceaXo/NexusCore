#include "CardRule.h"
#include <iostream>
#include <vector>
#include <cstdint>

static int tests = 0, passed = 0, failed = 0;

static const char* tn(CardType t) {
    switch (t) {
    case CardType::INVALID: return "INV";
    case CardType::SINGLE: return "SGL";
    case CardType::PAIR: return "PAI";
    case CardType::STRAIGHT: return "STR";
    case CardType::CONSECUTIVE_PAIRS: return "CPA";
    case CardType::TRIPLE_ONE: return "T31";
    case CardType::TRIPLE_TWO: return "T32";
    case CardType::QUAD_TWO: return "Q42";
    case CardType::QUAD_TWO_PAIRS: return "Q4P";
    case CardType::AIRPLANE: return "AIR";
    case CardType::BOMB: return "BOM";
    case CardType::ROCKET: return "ROK";
    default: return "???";
    }
}

static std::string cs(const std::vector<uint8_t>& v) {
    std::string s;
    for (auto c : v) s += std::to_string(c) + " ";
    if (!s.empty()) s.pop_back();
    return s;
}

#define T(desc, expr) do { \
    tests++; \
    if (expr) { passed++; } \
    else { \
        failed++; \
        std::cout << "  [FAIL] " << desc << std::endl; \
        std::cout << "         " << #expr << std::endl; \
    } \
} while(0)

#define TEQ(desc, a, b) T(desc, (a) == (b))

static uint8_t C(int rank, int suit) { return (uint8_t)(rank * 4 + suit); }
static std::vector<uint8_t> R(int rank, int n) {
    std::vector<uint8_t> v;
    for (int s = 0; s < n && s < 4; ++s) v.push_back(C(rank, s));
    return v;
}

// 辅助: 压牌检查并打印
static bool beats(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return CardRule::CanBeat(a, b);
}

int main() {
    // ===================================================================
    std::cout << "======== 顺子 STRAIGHT ========\n\n";

    // -- EvaluateType --
    TEQ("3张顺345",   CardRule::EvaluateType({C(0,0),C(1,0),C(2,0)}), CardType::STRAIGHT);
    TEQ("12张顺3-A",  CardRule::EvaluateType({C(0,0),C(1,0),C(2,0),C(3,0),C(4,0),
        C(5,0),C(6,0),C(7,0),C(8,0),C(9,0),C(10,0),C(11,0)}), CardType::STRAIGHT);
    TEQ("含2不是顺",   CardRule::EvaluateType({C(10,0),C(11,0),C(12,0)}), CardType::INVALID);
    TEQ("QKA2不是顺",  CardRule::EvaluateType({C(9,0),C(10,0),C(11,0),C(12,0)}), CardType::INVALID);
    TEQ("含王不是顺",  CardRule::EvaluateType({C(0,0),C(1,0),53}), CardType::INVALID);

    // -- CanBeat 核心规则: min低绝不能压min高; 同min比长度; min高须等长或更长 --
    // min低 → false
    T("345不压456(min=0<1)", !beats({C(0,0),C(1,0),C(2,0)}, {C(1,0),C(2,0),C(3,0)}));
    T("3456不压456(min=0<1)", !beats({C(0,0),C(1,0),C(2,0),C(3,0)}, {C(1,0),C(2,0),C(3,0)}));
    T("34567不压456(min=0<1)", !beats({C(0,0),C(1,0),C(2,0),C(3,0),C(4,0)},
                                      {C(1,0),C(2,0),C(3,0)}));

    // min同 → 长的赢
    T("45678压456(min同1,len5>3)",  beats({C(1,0),C(2,0),C(3,0),C(4,0),C(5,0)},
                                         {C(1,0),C(2,0),C(3,0)}));
    T("4567压456(min同1,len4>3)",    beats({C(1,0),C(2,0),C(3,0),C(4,0)},
                                         {C(1,0),C(2,0),C(3,0)}));
    T("456不压456(同min同长)",       !beats({C(1,0),C(2,0),C(3,0)},
                                         {C(1,1),C(2,1),C(3,1)}));
    T("456不压4567(同min但短)",      !beats({C(1,0),C(2,0),C(3,0)},
                                         {C(1,0),C(2,0),C(3,0),C(4,0)}));

    // min高 → 必须长度不短
    T("56789压345(min2>0,len5>=3)",  beats({C(2,0),C(3,0),C(4,0),C(5,0),C(6,0)},
                                         {C(0,0),C(1,0),C(2,0)}));
    T("5678压345(min2>0,len4>=3)",    beats({C(2,0),C(3,0),C(4,0),C(5,0)},
                                         {C(0,0),C(1,0),C(2,0)}));
    T("567不压3456(min2>0但len3<4)",  !beats({C(2,0),C(3,0),C(4,0)},
                                         {C(0,0),C(1,0),C(2,0),C(3,0)})); // ★ 拦短压长
    T("567不压34567(min2>0但len3<5)", !beats({C(2,0),C(3,0),C(4,0)},
                                         {C(0,0),C(1,0),C(2,0),C(3,0),C(4,0)}));
    T("8910JQKA压34567(min5>0,len7>=5)", beats(
        {C(5,0),C(6,0),C(7,0),C(8,0),C(9,0),C(10,0),C(11,0)},
        {C(0,0),C(1,0),C(2,0),C(3,0),C(4,0)}));

    // 极限: QKA(min=9) vs JQK(min=8): min更大 → 可压
    T("QKA压JQK(min9>8,len3>=3)", beats({C(9,0),C(10,0),C(11,0)},
                                       {C(8,0),C(9,0),C(10,0)}));
    // JQK(min=8) vs QKA(min=9): min更小 → 不可压
    T("JQK不压QKA(min8<9)",      !beats({C(8,0),C(9,0),C(10,0)},
                                       {C(9,0),C(10,0),C(11,0)}));

    std::cout << "\n======== 飞机 AIRPLANE ========\n\n";

    // -- EvaluateType --
    // 纯飞机
    TEQ("333444纯飞机", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)}), CardType::AIRPLANE);
    TEQ("333444555纯飞机K3", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),
        C(2,0),C(2,1),C(2,2)}), CardType::AIRPLANE);
    // 单翅
    TEQ("333444+5+6单翅", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),
        C(2,0),C(3,0)}), CardType::AIRPLANE);
    // 对翅
    TEQ("333444+55+66对翅", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),
        C(2,0),C(2,1),C(3,0),C(3,1)}), CardType::AIRPLANE);
    // 含2不是飞机
    TEQ("KKKAAA222含2", CardRule::EvaluateType({C(10,0),C(10,1),C(10,2),C(11,0),C(11,1),C(11,2),
        C(12,0),C(12,1),C(12,2)}), CardType::INVALID);

    // -- CanBeat: 同K同翅型互压 --
    // 纯vs纯
    T("纯444555压纯333444(同纯,min1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)}));
    T("纯555666压纯444555(同纯,min2>1)", beats(
        {C(2,0),C(2,1),C(2,2),C(3,0),C(3,1),C(3,2)},
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2)}));
    T("纯333444不压纯444555(min0<1)", !beats(
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)},
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2)}));

    // 单翅vs单翅
    T("单翅444555+6+7压单翅333444+5+6(同翅,min1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2),C(3,0),C(4,0)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),C(2,0),C(3,0)}));

    // 对翅vs对翅
    T("对翅444555压对翅333444(同翅,min1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2),C(3,0),C(3,1),C(4,0),C(4,1)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(3,0),C(3,1)}));

    // 不同翅型不能互压
    T("纯444555不压单翅333444(翅型不同)", !beats(
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),C(2,0),C(3,0)}));
    T("单翅444555不压对翅333444(翅型不同)", !beats(
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2),C(3,0),C(4,0)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(3,0),C(3,1)}));
    T("对翅444555不压纯333444(翅型不同)", !beats(
        {C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2),C(3,0),C(3,1),C(4,0),C(4,1)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)}));

    // 不同K不能互压
    T("纯K3不压纯K2(组数不同)", !beats(
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2),C(2,0),C(2,1),C(2,2)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)}));

    // 同K同min不能压
    T("纯333444不压纯333444(同K同min)", !beats(
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1),C(1,2)}));

    std::cout << "\n======== 三带一 TRIPLE_ONE ========\n\n";

    TEQ("333+4", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(1,0)}), CardType::TRIPLE_ONE);
    TEQ("333+A", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(11,0)}), CardType::TRIPLE_ONE);
    TEQ("222+3", CardRule::EvaluateType({C(12,0),C(12,1),C(12,2),C(0,0)}), CardType::TRIPLE_ONE);
    TEQ("333+大王", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),56}), CardType::TRIPLE_ONE);
    // 炸弹优先
    TEQ("3333是炸弹", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3)}), CardType::BOMB);

    // 三带一比主体点数，带牌不影响
    T("三4带3压三3带A(主体1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(0,0)},
        {C(0,0),C(0,1),C(0,2),C(11,0)}));
    T("三4带A压三3带5(主体1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(11,0)},
        {C(0,0),C(0,1),C(0,2),C(2,0)}));  // kicker用5(rank2),避免与主体同rank

    T("三3带A不压三4带3(主体0<1)", !beats(
        {C(0,0),C(0,1),C(0,2),C(11,0)},
        {C(1,0),C(1,1),C(1,2),C(0,0)}));

    // 同主体不能压 (点数相等)
    T("三3带4不压三3带5(同主体rank=0)", !beats(
        {C(0,0),C(0,1),C(0,2),C(1,0)},
        {C(0,0),C(0,1),C(0,2),C(2,0)}));
    // Wait, same body rank → can't beat. But they use different suits of the same rank.
    // C(0,0),C(0,1),C(0,2) vs C(0,0),C(0,1),C(0,2) - same triple, can't beat. Correct.

    // 不同张数不压
    T("三带一不压三带二", !beats(
        {C(0,0),C(0,1),C(0,2),C(11,0)},
        {C(1,0),C(1,1),C(1,2),C(11,0),C(11,1)}));

    std::cout << "\n======== 三带二 TRIPLE_TWO ========\n\n";

    TEQ("333+44", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(1,0),C(1,1)}), CardType::TRIPLE_TWO);
    TEQ("222+AA", CardRule::EvaluateType({C(12,0),C(12,1),C(12,2),C(11,0),C(11,1)}), CardType::TRIPLE_TWO);

    T("三4带对3压三3带对A(主体1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(0,0),C(0,1)},
        {C(0,0),C(0,1),C(0,2),C(11,0),C(11,1)}));
    T("三3带对A不压三4带对3(主体0<1)", !beats(
        {C(0,0),C(0,1),C(0,2),C(11,0),C(11,1)},
        {C(1,0),C(1,1),C(1,2),C(0,0),C(0,1)}));
    // 同主体不压
    T("三3带对4不压三3带对5(同主体)", !beats(
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1)},
        {C(0,0),C(0,1),C(0,2),C(2,0),C(2,1)}));

    std::cout << "\n======== 四带二 QUAD_TWO (6张) ========\n\n";

    TEQ("3333+4+5", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(2,0)}), CardType::QUAD_TWO);

    T("四4带两单压四3带两单(主体1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(1,3),C(0,0),C(2,0)},
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(2,0)}));
    T("四3带两单不压四4带两单(主体0<1)", !beats(
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(2,0)},
        {C(1,0),C(1,1),C(1,2),C(1,3),C(3,0),C(4,0)}));
    // 同主体不压
    T("四3带4,5不压四3带6,7(同主体)", !beats(
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(2,0)},
        {C(0,0),C(0,1),C(0,2),C(0,3),C(3,0),C(4,0)}));

    std::cout << "\n======== 四带两对 QUAD_TWO_PAIRS (8张) ========\n\n";

    TEQ("3333+44+55", CardRule::EvaluateType({C(0,0),C(0,1),C(0,2),C(0,3),
        C(1,0),C(1,1),C(2,0),C(2,1)}), CardType::QUAD_TWO_PAIRS);

    T("四4带两对压四3带两对(主体1>0)", beats(
        {C(1,0),C(1,1),C(1,2),C(1,3),C(0,0),C(0,1),C(2,0),C(2,1)},
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(1,1),C(2,0),C(2,1)}));
    T("四3带两对不压四4带两对(主体0<1)", !beats(
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(1,1),C(2,0),C(2,1)},
        {C(1,0),C(1,1),C(1,2),C(1,3),C(3,0),C(3,1),C(4,0),C(4,1)}));

    // ★ 核心: 四带二和四带两对是不同牌型，不能互压
    T("四带两单(6张)不压四带两对(8张-不同牌型)", !beats(
        {C(1,0),C(1,1),C(1,2),C(1,3),C(0,0),C(2,0)},
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(1,1),C(2,0),C(2,1)}));
    T("四带两对(8张)不压四带两单(6张-不同牌型)", !beats(
        {C(1,0),C(1,1),C(1,2),C(1,3),C(0,0),C(0,1),C(2,0),C(2,1)},
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(2,0)}));

    std::cout << "\n======== 跨类型尝试 ========\n\n";

    // 三带一不压三带二 (不同牌型)
    T("三带一不压三带二", !beats(
        {C(1,0),C(1,1),C(1,2),C(0,0)},
        {C(0,0),C(0,1),C(0,2),C(1,0),C(1,1)}));
    // 三带二不压四带二
    T("三带二不压四带二", !beats(
        {C(1,0),C(1,1),C(1,2),C(0,0),C(0,1)},
        {C(0,0),C(0,1),C(0,2),C(0,3),C(1,0),C(2,0)}));
    // 顺子不压连对
    T("顺子不压连对", !beats(
        {C(0,0),C(1,0),C(2,0)},
        {C(0,0),C(0,1),C(1,0),C(1,1)}));

    std::cout << "\n======== GetHints 冒烟 ========\n\n";

    {
        auto h = CardRule::GetHints({C(0,0),C(1,0),C(2,0),C(3,0)}, {});
        T("hints非空", !h.empty());
    }
    {
        std::vector<uint8_t> hand = {C(0,0),C(0,1),C(0,2), C(1,0),C(1,1),C(1,2),
                                      C(2,0),C(3,0),53,56};
        auto h = CardRule::GetHints(hand, {C(0,0)});  // 桌面单张3
        T("压单张有hints", !h.empty());
        for (auto& x : h)
            if (!CardRule::CanBeat(x, {C(0,0)})) {
                T("hint " + cs(x) + " 应能压桌面", false);
                break;
            }
    }

    // ===================================================================
    std::cout << "\n============================================\n";
    std::cout << "  " << tests << " tests: " << passed << " PASS / "
              << failed << " FAIL\n";
    std::cout << "============================================\n";
    return failed > 0 ? 1 : 0;
}
