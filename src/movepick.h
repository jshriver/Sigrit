// Staged move picker: TT move, good captures, killers, counter move, quiets, bad captures.
#pragma once
#include "history.h"
#include "position.h"

constexpr int PieceVal[7] = {100, 300, 300, 500, 900, 0, 0};

struct Picker {
    const Position &pos;
    const Histories &h;
    SStack *ss;
    Move ttm, k1, k2, cm;
    int stage;
    MoveList list;
    int scores[256];
    int idx = 0;
    Move bad[256];
    int nbad = 0, badIdx = 0;
    bool skipQuiets = false;

    Picker(const Position &p, const Histories &hist, SStack *s, Move tm, bool qs);
    Move next();

private:
    int cap_score(Move m) const;
    int quiet_score(Move m) const;
    void score_caps();
    void score_quiets();
    Move pick_best();
    bool is_quiet_ok(Move m) const;
};
