// Move list, generation types and perft. Generation itself is Position::generate (movegen.cpp).
#pragma once
#include "types.h"

struct MoveList {
    Move moves[256];
    int size = 0;
    void add(Move m) { moves[size++] = m; }
};

enum GenType { GEN_CAPTURES, GEN_QUIETS, GEN_ALL };

struct Position;
uint64_t perft(Position &pos, int depth);
