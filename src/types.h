// Basic types, move encoding, bit helpers and score constants shared by every module.
#pragma once
#include <cstdint>
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
inline void *amalloc(size_t sz) { return _aligned_malloc(sz, 64); }
inline void afree(void *p) { _aligned_free(p); }
#else
inline void *amalloc(size_t sz) { return aligned_alloc(64, (sz + 63) / 64 * 64); }
inline void afree(void *p) { free(p); }
#endif

typedef uint64_t U64;
typedef uint16_t Move;

enum { WHITE = 0, BLACK = 1 };
enum { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PT };
enum { NO_PIECE = 12 };
enum { NO_SQ = 64 };

inline int make_piece(int c, int pt) { return c * 6 + pt; }
inline int piece_type(int p) { return p % 6; }
inline int piece_color(int p) { return p / 6; }

inline int lsb(U64 b) { return __builtin_ctzll(b); }
inline int popcount(U64 b) { return __builtin_popcountll(b); }
inline int pop_lsb(U64 &b) { int s = lsb(b); b &= b - 1; return s; }
inline U64 sqbb(int s) { return 1ULL << s; }
inline int rank_of(int s) { return s >> 3; }
inline int file_of(int s) { return s & 7; }

// Move encoding: from(6) | to(6) | type(2) | promo(2)
enum { MT_NORMAL = 0, MT_PROMO = 1, MT_EP = 2, MT_CASTLE = 3 };
inline Move mk_move(int from, int to, int type = MT_NORMAL, int promoPt = KNIGHT) {
    return Move(from | (to << 6) | (type << 12) | ((promoPt - KNIGHT) << 14));
}
inline int move_from(Move m) { return m & 63; }
inline int move_to(Move m) { return (m >> 6) & 63; }
inline int move_type(Move m) { return (m >> 12) & 3; }
inline int move_promo(Move m) { return ((m >> 14) & 3) + KNIGHT; }
const Move MOVE_NONE = 0;
const Move MOVE_NULL = 65;

// Scores
const int MAX_PLY = 128;
const int MATE = 32000;
const int MATE_BOUND = MATE - 2 * MAX_PLY;
const int TB_WIN_SCORE = MATE_BOUND - 1;        // tablebase win, scored TB_WIN_SCORE - ply
const int TB_WIN_BOUND = MATE_BOUND - MAX_PLY;  // |score| >= this: proven mate or TB result
const int INF = 32001;
const int VALUE_NONE = 32002;
