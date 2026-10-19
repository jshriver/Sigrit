// Attack tables and square-set helpers. Sliders use PEXT on x86 with BMI2 and magic
// bitboards elsewhere (ARM, or -DNO_PEXT); both index the same tables.
#pragma once
#include "types.h"
#if defined(__BMI2__) && !defined(NO_PEXT)
#define USE_PEXT 1
#include <immintrin.h>
#endif

constexpr U64 FILE_A = 0x0101010101010101ULL;
constexpr U64 RANK_1 = 0xFFULL;

extern U64 PawnAtt[2][64], KnightAtt[64], KingAtt[64];
extern U64 RookMask[64], BishopMask[64];
extern U64 *RookPtr[64], *BishopPtr[64];
extern U64 RookMagic[64], BishopMagic[64];
extern unsigned RookShift[64], BishopShift[64];
extern U64 Between[64][64], Line[64][64];

#ifdef USE_PEXT
inline U64 rook_attacks(int s, U64 occ) { return RookPtr[s][_pext_u64(occ, RookMask[s])]; }
inline U64 bishop_attacks(int s, U64 occ) { return BishopPtr[s][_pext_u64(occ, BishopMask[s])]; }
#else
inline U64 rook_attacks(int s, U64 occ) {
    return RookPtr[s][((occ & RookMask[s]) * RookMagic[s]) >> RookShift[s]];
}
inline U64 bishop_attacks(int s, U64 occ) {
    return BishopPtr[s][((occ & BishopMask[s]) * BishopMagic[s]) >> BishopShift[s]];
}
#endif
inline U64 queen_attacks(int s, U64 occ) { return rook_attacks(s, occ) | bishop_attacks(s, occ); }
inline U64 piece_attacks(int pt, int s, U64 occ) {
    switch (pt) {
    case KNIGHT: return KnightAtt[s];
    case BISHOP: return bishop_attacks(s, occ);
    case ROOK: return rook_attacks(s, occ);
    case QUEEN: return queen_attacks(s, occ);
    case KING: return KingAtt[s];
    }
    return 0;
}

void init_attacks();
