#include "timeman.h"
#include <algorithm>
#include <chrono>
#include "types.h"

TimeManager Time;
int moveOverhead = 30;

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void TimeManager::init(const Limits &lim, int stm, int64_t t0) {
    startTime = t0;
    timed = false;
    int64_t time = stm == WHITE ? lim.wtime : lim.btime;
    int64_t inc = stm == WHITE ? lim.winc : lim.binc;
    if (lim.movetime > 0) {
        timed = true;
        softLimit = hardLimit = std::max<int64_t>(1, lim.movetime - moveOverhead);
        softLimit = hardLimit * 10;  // use full movetime
    } else if (time >= 0 && !lim.infinite) {
        timed = true;
        int64_t avail = std::max<int64_t>(1, time - moveOverhead);
        int mtg = lim.movestogo > 0 ? std::min(lim.movestogo, 40) : 22;
        double base = double(avail) / mtg + inc * 0.8;
        double cap = lim.movestogo == 1 ? avail * 0.85 : avail * 0.5;
        softLimit = int64_t(std::min(base * 0.7, cap));
        hardLimit = int64_t(std::min(base * 3.0, cap));
        softLimit = std::max<int64_t>(1, softLimit);
        hardLimit = std::max<int64_t>(1, hardLimit);
    }
}

int64_t TimeManager::elapsed() const { return now_ms() - startTime; }
