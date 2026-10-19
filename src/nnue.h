// NNUE evaluation for nets/net.nnue (768x8 king buckets -> 1024x2 -> 16 -> 32 -> 1, 8 output buckets)
#pragma once
#include "position.h"

namespace nnue {

const int HL = 1024;
const int L2 = 16;
const int L3 = 32;
const int QA = 255;
const int QB = 64;

struct alignas(64) Accumulator {
    int16_t v[2][HL];
    bool computed[2];
};

bool init();  // loads embedded net; returns false if invalid

// Evaluation state that lives alongside a Position (indexed by Position::sp).
struct Evaluator {
    Accumulator *acc;     // [MAX_STATES]
    DirtyPiece *dirty;    // [MAX_STATES]
    struct alignas(64) CacheEntry {
        int16_t v[HL];
        U64 bb[12];
    } cache[2][8];

    Evaluator();
    ~Evaluator();
    void attach(Position &pos);   // set pos.dirtyOut
    void reset(const Position &pos);  // full refresh at pos.sp; clears finny cache
    int evaluate(const Position &pos);
    void invalidate(int sp) { acc[sp].computed[0] = acc[sp].computed[1] = false; }

private:
    void update(const Position &pos, int persp);
    void refresh(const Position &pos, int persp, int16_t *out);
};

int evaluate_full(const Position &pos);  // slow reference, for testing

}  // namespace nnue
