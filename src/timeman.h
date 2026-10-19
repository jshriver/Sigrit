// Time management: per-move soft/hard limits from the UCI clock.
#pragma once
#include <cstdint>

struct Limits {
    int64_t wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    int movestogo = 0, depth = 0;
    bool infinite = false;
    bool ponder = false;
};

struct TimeManager {
    int64_t startTime = 0, softLimit = 0, hardLimit = 0;
    bool timed = false;
    void init(const Limits &lim, int stm, int64_t t0);
    int64_t elapsed() const;
};

extern TimeManager Time;
extern int moveOverhead;
int64_t now_ms();
