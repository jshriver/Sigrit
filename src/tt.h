// Shared transposition table (3-entry buckets, generation-based replacement).
#pragma once
#include <cstddef>
#include "types.h"

enum { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    uint16_t key;
    uint16_t move;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t genBound;
};

struct TTBucket {
    TTEntry e[3];
    uint16_t pad;
};

struct TT {
    TTBucket *table = nullptr;
    uint64_t count = 0;
    uint8_t gen = 0;
    void resize(size_t mb);
    void clear();
    void ensure();
    TTBucket *bucket(U64 key) const { return &table[(unsigned __int128)key * count >> 64]; }
    TTEntry *probe(U64 key, bool &hit);
    void store(TTEntry *e, U64 key, Move m, int score, int eval, int depth, int bound, bool pv = false);
    int hashfull() const;
    void new_search() {
        gen += 8;
        if (!gen) gen = 8;
    }
};
extern TT tt;
