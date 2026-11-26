// 電腦上的模擬器：在終端機輸入指令模擬硬體，印出狀態變化、OLED 內容與帳戶餘額。
//
//   pio run -e native && .pio/build/native/program
//   .pio/build/native/program < scripts/demo.txt     （跑完整示範流程）
//
// 每個指令執行後模擬器會自動前進 0.5 秒（讓平滑處理有讀數），tick 指令可以前進更久。
#include <Basket.h>
#include <Fakes.h>
#include <Json.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

using namespace core;

namespace {

constexpr uint32_t kStepMs = 50;
constexpr uint32_t kSettleMs = 500;

fakes::Rig rig;
Basket* basket = nullptr;
uint32_t nowMs = 0;
State lastState = State::Boot;
uint32_t lastSeq = 0;
int lastDisplayUpdates = -1;
bool dashboard = false;

std::string clock(uint32_t ms) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms % 1000) / 100);
    return buf;
}

// 終端機顯示寬度（中文字算 2 格）
int displayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w += 1; i += 1; }
        else if ((c & 0xE0) == 0xC0) { w += 1; i += 2; }
        else if ((c & 0xF0) == 0xE0) { w += 2; i += 3; }
        else { w += 2; i += 4; }
    }
    return w;
}

void printOled() {
    const int W = 22;
    std::printf("    ┌─ OLED ");
    for (int i = 0; i < W - 7; ++i) std::printf("─");
    std::printf("┐\n");
    for (auto& l : rig.display.text.line) {
        std::printf("    │ %s", l.c_str());
        for (int i = displayWidth(l); i < W - 1; ++i) std::printf(" ");
        std::printf("│\n");
    }
    std::printf("    └");
    for (int i = 0; i < W; ++i) std::printf("─");
    std::printf("┘\n");
}

void printBalances() {
    std::printf("    餘額：");
    if (basket->accounts().all().empty()) std::printf("（還沒有帳戶）");
    for (auto& kv : basket->accounts().all()) std::printf("%s=%d 元  ", kv.first.c_str(), kv.second);
    std::printf("\n");
}

// 把這段時間內的變化印出來
void report() {
    for (auto& e : basket->events().list()) {
        if (e.seq <= lastSeq) continue;
        lastSeq = e.seq;
        std::printf("  [%s] %-13s %s%s%s", clock(e.timeMs).c_str(), eventTypeName(e.type), e.uid.c_str(),
                    e.uid.empty() ? "" : " ", e.text.c_str());
        if (e.amount) std::printf("（%+d 元）", e.amount);
        std::printf("\n");
        if (e.type == EventType::Penalty || e.type == EventType::Reward) printBalances();
    }
    if (basket->state() != lastState) {
        std::printf("  狀態：%s → %s\n", stateName(lastState), stateName(basket->state()));
        lastState = basket->state();
    }
    if (rig.display.updates != lastDisplayUpdates) {
        lastDisplayUpdates = rig.display.updates;
        printOled();
    }
}

void advance(uint32_t ms) {
    uint32_t lockBefore = rig.lock.pulses;
    for (uint32_t t = 0; t < ms; t += kStepMs) {
        nowMs += kStepMs;
        basket->tick(nowMs);
        // 倒數中 OLED 每秒都在變，只印精簡的一行
        if (basket->state() == State::Countdown && lastState == State::Countdown &&
            rig.display.updates != lastDisplayUpdates) {
            lastDisplayUpdates = rig.display.updates;
            std::printf("  [%s] OLED：%s / %s  蜂鳴器%s\n", clock(nowMs).c_str(), rig.display.text.line[0].c_str(),
                        rig.display.text.line[1].c_str(), rig.buzzer.starts ? "嗶嗶響" : "");
        }
    }
    if (rig.lock.pulses != (int)lockBefore) std::printf("  電控鎖：通電 %u ms 開鎖\n", rig.lock.lastMs);
    report();
}

void printStatus() {
    Reading c = basket->current(), b = basket->baseline();
    DetectResult d = basket->detectNow();
    std::printf("  時間 %s，狀態 %s，借車者 %s\n", clock(nowMs).c_str(), stateName(basket->state()),
                basket->rider().empty() ? "（無）" : basket->rider().c_str());
    std::printf("  蓋子 %s，讀數%s：重量 %.1f g（基準 %.1f），距離 %d/%d mm（基準 %d/%d），紅外線 %s\n",
                rig.lid.closed ? "關" : "開", basket->sensorsReady() ? "" : "（不穩定）", c.weightG, b.weightG,
                c.distMm[0], c.distMm[1], b.distMm[0], b.distMm[1], c.irBlocked ? "擋住" : "通");
    std::printf("  判定：%s（重量 %d、測距0 %d、測距1 %d、紅外線 %d）\n", d.present ? "有東西" : "空的", d.byWeight,
                d.byDist[0], d.byDist[1], d.byIr);
    std::printf("  標記：%s%s%s\n", basket->hasTrash() ? "有垃圾 " : "", basket->hasLost() ? "有遺失物 " : "",
                basket->isClean() ? "乾淨" : "");
    for (auto& p : basket->pendings())
        std::printf("  待確認：%s，剩 %u 秒%s\n", p.owner.c_str(), (basket->pendingLeftMs(p, nowMs) + 999) / 1000,
                    p.cleared ? "（已被清空）" : "");
    printBalances();
    printOled();
}

void help() {
    std::printf(
        "指令：\n"
        "  tap <卡號>            感應 NFC 卡（借車 / 同一張卡再刷 = 還車）\n"
        "  weight <g>            設定秤重讀數\n"
        "  dist <0|1> <mm>       設定測距讀數\n"
        "  ir block|clear        紅外線對射 擋住 / 通\n"
        "  lid open|close        打開 / 關上蓋子\n"
        "  button                按一下開蓋按鈕\n"
        "  put <g> / take        快捷：開蓋放入 g 克物品（重量+測距+紅外線）/ 全部拿走，再關蓋\n"
        "  confirm trash|mine [卡號]  網頁確認（不給卡號 = 最舊的一筆）\n"
        "  tick <秒>             時間前進\n"
        "  calibrate             重新記錄空籃基準\n"
        "  set <名稱> <值>       調整設定：%s\n"
        "  status / json / help / quit\n",
        Config::keys());
}

void putItem(double g) {
    rig.lid.closed = false;
    advance(kSettleMs);
    rig.weight.grams = (float)g;
    rig.dist0.mm = g > 0 ? 180 : 300;
    rig.dist1.mm = g > 0 ? 220 : 300;
    rig.ir.blocked = g > 0;
    rig.lid.closed = true;
    std::printf("  （蓋子開→%s→關）\n", g > 0 ? "放入物品" : "拿出全部物品");
}

bool run(const std::string& line) {
    std::istringstream in(line);
    std::string cmd, a, b;
    in >> cmd >> a >> b;
    if (cmd.empty() || cmd[0] == '#') return true;

    uint32_t settle = kSettleMs;
    if (cmd == "tap" && !a.empty()) rig.card.tap(a);
    else if (cmd == "weight" && !a.empty()) rig.weight.grams = (float)std::atof(a.c_str());
    else if (cmd == "dist" && (a == "0" || a == "1") && !b.empty()) (a == "0" ? rig.dist0 : rig.dist1).mm = std::atoi(b.c_str());
    else if (cmd == "ir" && (a == "block" || a == "clear")) rig.ir.blocked = a == "block";
    else if (cmd == "lid" && (a == "open" || a == "close")) rig.lid.closed = a == "close";
    else if (cmd == "button") {
        rig.button.pressed = true;
        advance(100);
        rig.button.pressed = false;
    } else if (cmd == "put" && !a.empty()) putItem(std::atof(a.c_str()));
    else if (cmd == "take") putItem(0);
    else if (cmd == "confirm" && (a == "trash" || a == "mine")) {
        if (!basket->confirm(b, a == "trash" ? ConfirmKind::Trash : ConfirmKind::Mine, nowMs))
            std::printf("  沒有待確認的物品%s\n", b.empty() ? "" : "（卡號不符）");
    } else if (cmd == "tick" && !a.empty()) settle = (uint32_t)(std::atof(a.c_str()) * 1000);
    else if (cmd == "calibrate") {
        if (!basket->calibrate(nowMs)) std::printf("  讀數不穩定（蓋子要關著），無法記錄基準\n");
    } else if (cmd == "set" && !a.empty() && !b.empty()) {
        if (basket->setConfig(a, std::atof(b.c_str()))) std::printf("  設定已更新：%s\n", basket->config().serialize().c_str());
        else std::printf("  設定失敗（名稱錯誤或值不合法；回饋金必須小於罰款、開鎖最多 1000 ms）\n");
    } else if (cmd == "status") { printStatus(); return true; }
    else if (cmd == "json") {
        const auto state = stateJson(*basket, nowMs);
        if (dashboard) {
            std::printf("{\"state\":%s,\"clockMs\":%u,\"storage\":%s,"
                        "\"inputs\":[%.9g,%d,%d,%d,%d],\"signals\":{\"lockPulses\":%d,\"buzzerHz\":%u}}\n",
                        state.c_str(), nowMs, jsonEscape(rig.storage.data["state"]).c_str(),
                        rig.weight.grams, rig.dist0.mm, rig.dist1.mm, rig.ir.blocked,
                        rig.lid.closed, rig.lock.pulses, rig.buzzer.freq);
        } else {
            std::printf("%s\n", state.c_str());
        }
        return true;
    }
    else if (cmd == "help") { help(); return true; }
    else if (cmd == "quit" || cmd == "exit") return false;
    else {
        std::printf("  看不懂的指令：%s（輸入 help 看說明）\n", line.c_str());
        return true;
    }
    advance(settle);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool tty = isatty(0);
    dashboard = argc == 2 && std::strcmp(argv[1], "--dashboard") == 0;
    if (dashboard) {
        // 啟動設定一行，接著讀精確長度的 Basket 快照（可包含換行）。
        std::string header;
        std::getline(std::cin, header);
        size_t size = 0;
        std::istringstream input(header);
        if (!(input >> rig.weight.grams >> rig.dist0.mm >> rig.dist1.mm >> rig.ir.blocked >> rig.lid.closed >> size)) return 1;
        if (size) {
            std::string state(size, '\0');
            if (!std::cin.read(state.data(), size)) return 1;
            rig.storage.data["state"] = std::move(state);
        }
    }
    Basket b(rig.hw());
    basket = &b;
    std::printf("智慧 Ubike 籃子模擬器（輸入 help 看指令）\n");
    b.begin(nowMs);
    advance(1000);
    if (!b.hasBaseline()) b.calibrate(nowMs);  // 只有首次啟動時記錄已知的空籃。
    advance(50);
    printBalances();

    std::string line;
    while (true) {
        if (tty) std::printf("[%s]> ", clock(nowMs).c_str());
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        if (!tty && !line.empty()) {
            if (line[0] == '#') std::printf("\n%s\n", line.c_str());
            else std::printf("[%s]> %s\n", clock(nowMs).c_str(), line.c_str());
        }
        if (!run(line)) break;
    }
    std::printf("\n最終狀態：\n");
    printStatus();
    return 0;
}
