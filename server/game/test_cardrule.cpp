#include "CardRule.h"
#include <iostream>
#include <fstream>
#include <random>
#include <cstring>
#include <set>
#include <sstream>

// 点数名
static const char* rankName(int r) {
    switch (r) {
    case 0: return "3"; case 1: return "4"; case 2: return "5";
    case 3: return "6"; case 4: return "7"; case 5: return "8";
    case 6: return "9"; case 7: return "10"; case 8: return "J";
    case 9: return "Q"; case 10: return "K"; case 11: return "A";
    case 12: return "2"; case 13: return "小王"; case 14: return "大王";
    default: return "?";
    }
}

// 牌名: 花色+点数
static std::string cardName(uint8_t c) {
    static const char* suit[] = {"方","梅","红","黑"};
    int r = c / 4;
    if (r == 13) return "小王";
    if (r == 14) return "大王";
    return std::string(suit[c % 4]) + rankName(r);
}

// 一手牌的字符串
static std::string handStr(const std::vector<uint8_t>& cards) {
    std::string s;
    for (auto c : cards) s += cardName(c) + " ";
    return s;
}

// 牌型名
static const char* typeName(CardType t) {
    switch (t) {
    case CardType::INVALID: return "非法";
    case CardType::SINGLE: return "单张";
    case CardType::PAIR: return "对子";
    case CardType::STRAIGHT: return "顺子";
    case CardType::CONSECUTIVE_PAIRS: return "连对";
    case CardType::TRIPLE_ONE: return "三带一";
    case CardType::TRIPLE_TWO: return "三带二";
    case CardType::QUAD_TWO: return "四带二";
    case CardType::AIRPLANE: return "飞机";
    case CardType::BOMB: return "炸弹";
    case CardType::ROCKET: return "王炸";
    default: return "???";
    }
}

int main() {
    std::ofstream f("test_cardrule_output.txt");
    auto log = [&](const std::string& s) { std::cout << s; f << s; };

    std::mt19937 rng(42);
    int round = 0;
    int tests = 0, pass = 0, fail = 0;

    auto check = [&](bool cond, const std::string& desc) {
        tests++;
        if (cond) { pass++; log("  [PASS] " + desc + "\n\n"); }
        else      { fail++; log("  [FAIL] " + desc + "\n\n"); }
    };

    // ===================================================================
    // 手工构造精确测试：每种牌型的基础压制
    // ===================================================================
    log("===== 手工精确测试 =====\n\n");

    // -- 单张 --
    round++;
    log("--- R" + std::to_string(round) + " 单张压制 ---\n");
    {
        std::vector<uint8_t> p3{0};       // 方块3
        std::vector<uint8_t> p4{4};       // 方块4
        std::vector<uint8_t> p2{48};      // 黑桃2
        std::vector<uint8_t> pJ{53};      // 小王
        std::vector<uint8_t> pD{56};      // 大王
        check(CardRule::CanBeat(p4, p3), "方块4 压 方块3");
        check(!CardRule::CanBeat(p3, p4), "方块3 不压 方块4");
        check(CardRule::CanBeat(p2, p4), "黑桃2 压 方块4");
        check(CardRule::CanBeat(pJ, p2), "小王 压 黑桃2");
        check(CardRule::CanBeat(pD, pJ), "大王 压 小王");
    }

    // -- 对子 --
    round++;
    log("--- R" + std::to_string(round) + " 对子压制 ---\n");
    {
        // {0,1} = 方块3+梅花3 同rank0; {4,5} = 方块4+梅花4 同rank1
        check(!CardRule::CanBeat({0,1}, {4,5}), "对3 不压 对4 (rank0<1)");
        check(CardRule::CanBeat({4,5}, {0,1}), "对4 压 对3 (rank1>0)");
    }

    // -- 顺子: 比最小点数 --
    round++;
    log("--- R" + std::to_string(round) + " 顺子压制 (比最小点数) ---\n");
    {
        // 3-4-5 = ranks 0,1,2; 4-5-6 = ranks 1,2,3
        std::vector<uint8_t> s345  {0, 4, 8};     // 3-4-5 (min=0)
        std::vector<uint8_t> s456  {4, 8, 12};    // 4-5-6 (min=1)
        std::vector<uint8_t> s3456 {0, 4, 8, 12}; // 3-4-5-6 (min=0)
        std::vector<uint8_t> s4567 {4, 8, 12, 16};// 4-5-6-7 (min=1)

        check(CardRule::CanBeat(s456, s345), "456 压 345 (起点 1>0)");
        check(!CardRule::CanBeat(s345, s456), "345 不压 456 (起点 0<1)");
        check(CardRule::CanBeat(s4567, s456), "4567 压 456 (同起点1, 更长)");
        // 关键：3456 (min=0) 不能压 456 (min=1)
        check(!CardRule::CanBeat(s3456, s456), "3456 不压 456 (起点0<1)");
    }

    // -- 三带一 --
    round++;
    log("--- R" + std::to_string(round) + " 三带一压制 ---\n");
    {
        // 三个5带方块3 = ranks 2,2,2,0; 三个6带方块3 = ranks 3,3,3,0
        auto t5_1 = std::vector<uint8_t>{0, 8,9,10};     // 三个5+单3 (主体rank2)
        auto t6_1 = std::vector<uint8_t>{0, 12,13,14};   // 三个6+单3 (主体rank3)
        auto t8_1 = std::vector<uint8_t>{4, 20,21,22};   // 三个8+单4 (主体rank5)

        check(CardRule::CanBeat(t6_1, t5_1), "三6带3 压 三5带3 (主体3>2)");
        check(!CardRule::CanBeat(t5_1, t6_1), "三5带3 不压 三6带3");
        check(CardRule::CanBeat(t8_1, t6_1), "三8带4 压 三6带3 (主体5>3)");
    }

    // -- 三带二 --
    round++;
    log("--- R" + std::to_string(round) + " 三带二压制 ---\n");
    {
        // 三个4+对3 (主体rank1); 三个5+对3 (主体rank2)
        auto t4_2 = std::vector<uint8_t>{0,1, 4,5,6};    // 三个4+对3
        auto t5_2 = std::vector<uint8_t>{0,1, 8,9,10};   // 三个5+对3
        check(CardRule::CanBeat(t5_2, t4_2), "三5带对3 压 三4带对3");
    }

    // -- 四带二 --
    round++;
    log("--- R" + std::to_string(round) + " 四带二压制 ---\n");
    {
        // 四个4+单3+单5 (主体rank1); 四个6+单3+单4 (主体rank3)
        auto q4_2 = std::vector<uint8_t>{0,8, 4,5,6,7};   // 四个4+单3+单5
        auto q6_2 = std::vector<uint8_t>{0,4, 12,13,14,15};// 四个6+单3+单4
        check(CardRule::CanBeat(q6_2, q4_2), "四6带两单 压 四4带两单 (主体3>1)");
    }

    // -- 炸弹 --
    round++;
    log("--- R" + std::to_string(round) + " 炸弹压制 ---\n");
    {
        auto bomb3x3 = std::vector<uint8_t>{0,1,2};        // 三张3
        auto bomb4x3 = std::vector<uint8_t>{4,5,6};        // 三张4
        auto bomb4x4 = std::vector<uint8_t>{4,5,6,7};      // 四张4
        auto bomb5x4 = std::vector<uint8_t>{8,9,10,11};    // 四张5

        check(CardRule::CanBeat(bomb4x3, bomb3x3), "三张4 压 三张3 (同张数比点数)");
        check(CardRule::CanBeat(bomb4x4, bomb3x3), "四张4 压 三张3 (张数多)");
        check(CardRule::CanBeat(bomb4x4, bomb4x3), "四张4 压 三张4 (张数多)");
        check(CardRule::CanBeat(bomb5x4, bomb4x4), "四张5 压 四张4 (同张数比点数)");
        // 炸弹压普通
        check(CardRule::CanBeat(bomb3x3, {0}),      "三张3炸弹 压 单张");
        check(CardRule::CanBeat(bomb3x3, {0,1}),    "三张3炸弹 压 对子");
        std::vector<uint8_t> s345b{0,4,8};
        check(CardRule::CanBeat(bomb3x3, s345b),    "三张3炸弹 压 345顺子");
    }

    // -- 王炸 --
    round++;
    log("--- R" + std::to_string(round) + " 王炸压制 ---\n");
    {
        auto rocket = std::vector<uint8_t>{53,56};
        auto bomb4x2 = std::vector<uint8_t>{48,49,50,51}; // 四张2
        check(CardRule::CanBeat(rocket, bomb4x2), "王炸 压 四张2炸弹");
        check(CardRule::CanBeat(rocket, {0}),     "王炸 压 单张3");
        check(!CardRule::CanBeat(bomb4x2, rocket), "四张2炸弹 不压 王炸");
        check(!CardRule::CanBeat({0}, rocket),     "单张3 不压 王炸");
    }

    // -- 飞机 --
    round++;
    log("--- R" + std::to_string(round) + " 飞机压制 ---\n");
    {
        // 333-444 纯飞机 (K=2, ranks 0,1)
        auto plane34_pure = std::vector<uint8_t>{0,1,2, 4,5,6};
        // 444-555 纯飞机 (K=2, ranks 1,2)
        auto plane45_pure = std::vector<uint8_t>{4,5,6, 8,9,10};
        // 555-666 纯飞机 (K=2, ranks 2,3)
        auto plane56_pure = std::vector<uint8_t>{8,9,10, 12,13,14};

        check(CardRule::CanBeat(plane45_pure, plane34_pure), "444555 压 333444 (同K,起点1>0)");
        check(CardRule::CanBeat(plane56_pure, plane45_pure), "555666 压 444555 (同K,起点2>1)");
        check(!CardRule::CanBeat(plane34_pure, plane45_pure), "333444 不压 444555");

        // 333-444-5-6 带单翅 (K=2, 8张)
        auto plane34_1 = std::vector<uint8_t>{0,1,2, 4,5,6, 8,12};     // 翅膀:5,6
        auto plane45_1 = std::vector<uint8_t>{4,5,6, 8,9,10, 0,1};     // 翅膀:3,3
        check(CardRule::CanBeat(plane45_1, plane34_1), "333444带单翅 压... (同K,起点1>0)");

        // 333-444-55-66 带对翅 (K=2, 10张)
        auto plane34_2 = std::vector<uint8_t>{0,1,2, 4,5,6, 8,9, 12,13};   // 翅膀:对5,对6
        auto plane56_2 = std::vector<uint8_t>{8,9,10, 12,13,14, 0,1, 4,5};  // 翅膀:对3,对4
        check(CardRule::CanBeat(plane56_2, plane34_2), "555666带对翅 压 333444带对翅 (同K,起点2>0)");
    }

    // ===================================================================
    // 随机牌局模拟 (10000回合)
    // ===================================================================
    log("\n===== 随机牌局模拟 (10000回合) =====\n\n");

    std::vector<uint8_t> deck(54);
    for (int i = 0; i < 54; ++i) deck[i] = i;

    int sim_rounds = 0, sim_free = 0, sim_beat = 0, sim_pass = 0;
    int sim_bomb_used = 0, sim_rocket_used = 0, sim_plane_used = 0;
    const int TARGET = 10000;

    while (sim_rounds < TARGET) {
        std::shuffle(deck.begin(), deck.end(), rng);
        std::vector<std::vector<uint8_t>> hands(3);
        for (int p = 0; p < 3; ++p) {
            for (int i = 0; i < 10; ++i)
                hands[p].push_back(deck[p * 10 + i]);
            CardRule::SortHand(hands[p]);
        }

        std::vector<uint8_t> table;
        int turn = 0, pass_cnt = 0, prev_winner = -1;

        for (int r = 0; r < 200 && sim_rounds < TARGET; ++r) {
            int player = turn % 3;
            auto hints = CardRule::GetHints(hands[player], table);
            bool freePlay = table.empty() || (pass_cnt >= 2 && prev_winner == player);

            std::vector<uint8_t> play;
            std::vector<uint8_t> old_table = table;

            if (freePlay) {
                hints = CardRule::GetHints(hands[player], {});
                if (hints.empty()) break; // 空手牌
                play = hints[0];
                table = play;
                pass_cnt = 1;
                prev_winner = player;
                sim_free++;
            } else if (!hints.empty()) {
                play = hints[0];
                table = play;
                pass_cnt = 1;
                prev_winner = player;
                sim_beat++;
            } else {
                pass_cnt++;
                turn = (turn + 1) % 3;
                sim_pass++;
                if (pass_cnt >= 3) { table.clear(); pass_cnt = 0; }
                continue;
            }

            // 从手牌移除
            std::vector<uint8_t> newHand;
            for (auto c : hands[player]) {
                bool used = false;
                for (auto p : play) if (c == p) { used = true; break; }
                if (!used) newHand.push_back(c);
            }
            hands[player] = newHand;

            auto t = CardRule::EvaluateType(play);
            sim_rounds++;
            round++;

            // 统计
            if (t == CardType::BOMB) sim_bomb_used++;
            else if (t == CardType::ROCKET) sim_rocket_used++;
            else if (t == CardType::AIRPLANE) sim_plane_used++;

            // 每1000回合报告一次进度
            if (sim_rounds % 1000 == 0) {
                log("  进度: " + std::to_string(sim_rounds) + "/" + std::to_string(TARGET)
                    + " rounds, " + std::to_string(pass) + "P/" + std::to_string(fail) + "F\n");
            }

            // 验证合法性
            if (freePlay) {
                check(t != CardType::INVALID,
                      "自由出牌 " + handStr(play) + " 应为合法牌型");
            } else {
                check(CardRule::CanBeat(play, old_table),
                      handStr(play) + " 应能压过 " + handStr(old_table));
            }

            turn = (turn + 1) % 3;
            if (pass_cnt >= 3) { table.clear(); pass_cnt = 0; }

            if (hands[player].empty()) break;
        }
    }

    log("\n  统计: 自由出牌=" + std::to_string(sim_free)
        + "  压制=" + std::to_string(sim_beat)
        + "  Pass=" + std::to_string(sim_pass)
        + "  炸弹=" + std::to_string(sim_bomb_used)
        + "  王炸=" + std::to_string(sim_rocket_used)
        + "  飞机=" + std::to_string(sim_plane_used) + "\n");

    // ===================================================================
    // 针对性边界测试
    // ===================================================================
    log("\n===== 边界测试 =====\n\n");

    // 非法牌型
    round++;
    log("--- R" + std::to_string(round) + " 非法牌型 ---\n");
    check(CardRule::EvaluateType({0,4,8,12,16}) == CardType::STRAIGHT, "34567是顺子");
    check(CardRule::EvaluateType({0,1}) == CardType::PAIR, "方块3+梅花3是对子");
    check(CardRule::EvaluateType({0,4,1}) == CardType::INVALID, "三张不同点数非法");
    check(CardRule::EvaluateType({0,0,0,4,4,4,52}) == CardType::INVALID, "非法: 顺子含王");
    check(CardRule::EvaluateType({48,49,50,51}) == CardType::BOMB, "四张2是炸弹");
    // 顺子不能含2
    auto straightWith2 = std::vector<uint8_t>{40,44,48}; // 10-J-2 (ranks 7,8,12)
    check(CardRule::EvaluateType(straightWith2) != CardType::STRAIGHT, "10-J-2不是顺子(含2)");
    // 顺子不能含王
    auto straightWithJoker = std::vector<uint8_t>{0,4,53}; // 3-4-小王
    check(CardRule::EvaluateType(straightWithJoker) != CardType::STRAIGHT, "3-4-小王不是顺子");

    // 同类型非顺子必须同张数
    round++;
    log("--- R" + std::to_string(round) + " 同张数检查 ---\n");
    check(!CardRule::CanBeat({0}, {0,1}), "单张不压对子(不同牌型)");
    check(!CardRule::CanBeat({0,1}, {0}), "对子不压单张(不同牌型)");
    // 三带一4张不能压5张的三带二
    auto t5_1 = std::vector<uint8_t>{0, 8,9,10};   // 三带一 (4张)
    auto t5_2 = std::vector<uint8_t>{4,5, 8,9,10}; // 三带二 (5张)
    check(!CardRule::CanBeat(t5_2, t5_1), "三带二不压三带一(不同牌型)");
    check(!CardRule::CanBeat(t5_1, t5_2), "三带一不压三带二(不同牌型)");

    // 飞机边界
    round++;
    log("--- R" + std::to_string(round) + " 飞机边界 ---\n");
    // 飞机 vs 炸弹
    auto plane34 = std::vector<uint8_t>{0,1,2, 4,5,6};
    auto bombAny = std::vector<uint8_t>{12,13,14}; // 三张6
    check(CardRule::CanBeat(bombAny, plane34), "炸弹 压 飞机");
    check(!CardRule::CanBeat(plane34, bombAny), "飞机 不压 炸弹");

    // 不同K的飞机不能互压
    // 333-444 (K=2)
    auto planeK2 = std::vector<uint8_t>{0,1,2, 4,5,6};
    // 333-444-555 (K=3)
    auto planeK3 = std::vector<uint8_t>{0,1,2, 4,5,6, 8,9,10};
    check(!CardRule::CanBeat(planeK3, planeK2), "K=3飞机不压K=2飞机(不同组数)");

    // 点数更大的K=2飞机能压
    auto plane45 = std::vector<uint8_t>{4,5,6, 8,9,10}; // 444-555
    check(CardRule::CanBeat(plane45, plane34), "444555 压 333444");

    // ===================================================================
    // 总结
    // ===================================================================
    log("\n===== 结果: " + std::to_string(pass) + " PASS / "
        + std::to_string(fail) + " FAIL (共" + std::to_string(tests) + "项) =====\n");

    f.close();
    std::cout << "\n输出已写入 test_cardrule_output.txt\n";
    return fail > 0 ? 1 : 0;
}
