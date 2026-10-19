#include "bitboard.h"

U64 PawnAtt[2][64], KnightAtt[64], KingAtt[64];
U64 RookMask[64], BishopMask[64];
U64 *RookPtr[64], *BishopPtr[64];
U64 RookMagic[64], BishopMagic[64];
unsigned RookShift[64], BishopShift[64];
U64 Between[64][64], Line[64][64];
static U64 RookTable[102400], BishopTable[5248];

static U64 slide(int s, U64 occ, const int dirs[4][2]) {
    U64 r = 0;
    for (int d = 0; d < 4; d++) {
        int f = file_of(s), k = rank_of(s);
        while (true) {
            f += dirs[d][0];
            k += dirs[d][1];
            if (f < 0 || f > 7 || k < 0 || k > 7) break;
            r |= sqbb(k * 8 + f);
            if (occ & sqbb(k * 8 + f)) break;
        }
    }
    return r;
}
static const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int BishopDirs[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

// Fills a slider's table for square s. With PEXT the index is pext(occ, mask), i.e. the
// subset's position in carry-rippler order. Otherwise searches a magic multiplier (fixed seed,
// so the result is deterministic) mapping every subset to an index without a wrong collision.
static void init_slider(int s, U64 mask, const int dirs[4][2], U64 *table, U64 &magic, unsigned &shift) {
    static U64 occ[4096], att[4096];
    int n = 0;
    U64 b = 0;
    do {
        occ[n] = b;  // only the magic search needs the subsets themselves
        att[n++] = slide(s, b, dirs);
        b = (b - mask) & mask;
    } while (b);
    shift = 64 - popcount(mask);
#ifdef USE_PEXT
    for (int i = 0; i < n; i++) table[i] = att[i];
    magic = 0;
    (void)occ;
#else
    static int stamp[4096], epoch = 0;
    static U64 seed = 0x5EED5EED12345678ULL;
    auto rnd = [&]() {
        seed ^= seed >> 12, seed ^= seed << 25, seed ^= seed >> 27;
        return seed * 2685821657736338717ULL;
    };
    while (true) {
        U64 m = rnd() & rnd() & rnd();
        if (popcount((mask * m) >> 56) < 6) continue;
        epoch++;
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            unsigned idx = unsigned((occ[i] * m) >> shift);
            if (stamp[idx] != epoch) stamp[idx] = epoch, table[idx] = att[i];
            else if (table[idx] != att[i]) ok = false;
        }
        if (ok) {
            magic = m;
            return;
        }
    }
#endif
}

void init_attacks() {
    for (int s = 0; s < 64; s++) {
        int f = file_of(s), r = rank_of(s);
        auto add = [&](U64 &b, int df, int dr) {
            int nf = f + df, nr = r + dr;
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) b |= sqbb(nr * 8 + nf);
        };
        PawnAtt[WHITE][s] = PawnAtt[BLACK][s] = KnightAtt[s] = KingAtt[s] = 0;
        add(PawnAtt[WHITE][s], -1, 1);
        add(PawnAtt[WHITE][s], 1, 1);
        add(PawnAtt[BLACK][s], -1, -1);
        add(PawnAtt[BLACK][s], 1, -1);
        int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (auto &d : kn) add(KnightAtt[s], d[0], d[1]);
        for (int df = -1; df <= 1; df++)
            for (int dr = -1; dr <= 1; dr++)
                if (df || dr) add(KingAtt[s], df, dr);
    }
    U64 *rp = RookTable, *bp = BishopTable;
    for (int s = 0; s < 64; s++) {
        U64 edges = ((RANK_1 | (RANK_1 << 56)) & ~(RANK_1 << (8 * rank_of(s)))) |
                    ((FILE_A | (FILE_A << 7)) & ~(FILE_A << file_of(s)));
        RookMask[s] = slide(s, 0, RookDirs) & ~edges;
        BishopMask[s] = slide(s, 0, BishopDirs) & ~edges;
        RookPtr[s] = rp;
        BishopPtr[s] = bp;
        int rn = 1 << popcount(RookMask[s]), bn = 1 << popcount(BishopMask[s]);
        init_slider(s, RookMask[s], RookDirs, rp, RookMagic[s], RookShift[s]);
        init_slider(s, BishopMask[s], BishopDirs, bp, BishopMagic[s], BishopShift[s]);
        rp += rn;
        bp += bn;
    }
    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) {
            Between[a][b] = Line[a][b] = 0;
            if (a == b) continue;
            if (slide(a, 0, RookDirs) & sqbb(b)) {
                Between[a][b] = slide(a, sqbb(b), RookDirs) & slide(b, sqbb(a), RookDirs);
                Line[a][b] = (slide(a, 0, RookDirs) & slide(b, 0, RookDirs)) | sqbb(a) | sqbb(b);
            } else if (slide(a, 0, BishopDirs) & sqbb(b)) {
                Between[a][b] = slide(a, sqbb(b), BishopDirs) & slide(b, sqbb(a), BishopDirs);
                Line[a][b] = (slide(a, 0, BishopDirs) & slide(b, 0, BishopDirs)) | sqbb(a) | sqbb(b);
            }
        }
}

