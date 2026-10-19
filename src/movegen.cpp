#include "position.h"

static inline void add_promos(MoveList &ml, int from, int to, GenType gt) {
    if (gt != GEN_QUIETS) ml.add(mk_move(from, to, MT_PROMO, QUEEN));
    if (gt != GEN_CAPTURES) {
        ml.add(mk_move(from, to, MT_PROMO, KNIGHT));
        ml.add(mk_move(from, to, MT_PROMO, ROOK));
        ml.add(mk_move(from, to, MT_PROMO, BISHOP));
    }
}

void Position::generate(GenType gt, MoveList &ml) const {
    int us = stm, them = us ^ 1;
    U64 o = occ(), empty = ~o, enemy = byColor[them];
    U64 pawns = pieces(us, PAWN);
    U64 promoRank = us == WHITE ? (RANK_1 << 56) : RANK_1;
    U64 rank3 = us == WHITE ? (RANK_1 << 16) : (RANK_1 << 40);
    int up = us == WHITE ? 8 : -8;
    auto shift = [&](U64 b, int d) { return d > 0 ? b << d : b >> -d; };
    U64 notA = ~FILE_A, notH = ~(FILE_A << 7);

    U64 push1 = shift(pawns, up) & empty;
    U64 push2 = shift(push1 & rank3, up) & empty;
    // left capture (towards file a): white +7, black -9; right: white +9, black -7
    int dl = us == WHITE ? 7 : -9, dr = us == WHITE ? 9 : -7;
    U64 capL = shift(pawns & notA, dl) & enemy;
    U64 capR = shift(pawns & notH, dr) & enemy;

    // promotions
    {
        U64 b = push1 & promoRank;
        while (b) {
            int t = pop_lsb(b);
            add_promos(ml, t - up, t, gt);
        }
        b = capL & promoRank;
        while (b) {
            int t = pop_lsb(b);
            add_promos(ml, t - dl, t, gt);
        }
        b = capR & promoRank;
        while (b) {
            int t = pop_lsb(b);
            add_promos(ml, t - dr, t, gt);
        }
    }
    if (gt != GEN_QUIETS) {
        U64 b = capL & ~promoRank;
        while (b) {
            int t = pop_lsb(b);
            ml.add(mk_move(t - dl, t));
        }
        b = capR & ~promoRank;
        while (b) {
            int t = pop_lsb(b);
            ml.add(mk_move(t - dr, t));
        }
        int ep = st[sp].ep;
        if (ep != NO_SQ) {
            U64 att = PawnAtt[them][ep] & pawns;
            while (att) ml.add(mk_move(pop_lsb(att), ep, MT_EP));
        }
    }
    if (gt != GEN_CAPTURES) {
        U64 b = push1 & ~promoRank;
        while (b) {
            int t = pop_lsb(b);
            ml.add(mk_move(t - up, t));
        }
        b = push2;
        while (b) {
            int t = pop_lsb(b);
            ml.add(mk_move(t - 2 * up, t));
        }
    }
    U64 targets = gt == GEN_CAPTURES ? enemy : gt == GEN_QUIETS ? empty : ~byColor[us];
    for (int pt = KNIGHT; pt <= KING; pt++) {
        U64 b = pieces(us, pt);
        while (b) {
            int f = pop_lsb(b);
            U64 a = piece_attacks(pt, f, o) & targets;
            while (a) ml.add(mk_move(f, pop_lsb(a)));
        }
    }
    if (gt != GEN_CAPTURES && !st[sp].checkers) {
        int c = st[sp].castling, ksq = king_sq(us);
        for (int r = 2 * us; r < 2 * us + 2; r++) {
            if (!(c & (1 << r)) || (o & castlePath[r])) continue;
            int rsq = castleRookSq[r];
            // attacks are tested without king and castling rook (in Chess960 the rook may shield a square)
            U64 o2 = o ^ sqbb(ksq) ^ sqbb(rsq), path = castleKingPath[r];
            bool ok = true;
            while (path && ok) ok = !sq_attacked(pop_lsb(path), them, o2);
            if (ok) ml.add(mk_move(ksq, rsq, MT_CASTLE));
        }
    }
}

bool Position::is_legal(Move m) const {
    int from = move_from(m), to = move_to(m), us = stm, them = us ^ 1;
    int ksq = king_sq(us);
    const StateInfo &x = st[sp];
    if (move_type(m) == MT_EP) {
        int cap = to - (us == WHITE ? 8 : -8);
        U64 o = (occ() ^ sqbb(from) ^ sqbb(cap)) | sqbb(to);
        if (rook_attacks(ksq, o) & (pieces(them, ROOK) | pieces(them, QUEEN))) return false;
        if (bishop_attacks(ksq, o) & (pieces(them, BISHOP) | pieces(them, QUEEN))) return false;
        if (x.checkers & ~sqbb(cap) & (byType[KNIGHT] | byType[PAWN])) return false;
        return true;
    }
    if (from == ksq) {
        if (move_type(m) == MT_CASTLE) return true;  // generator verified path
        return !sq_attacked(to, them, occ() ^ sqbb(from));
    }
    if (x.checkers) {
        if (x.checkers & (x.checkers - 1)) return false;
        int chk = lsb(x.checkers);
        if (!((Between[ksq][chk] | x.checkers) & sqbb(to))) return false;
    }
    if ((x.pinned & sqbb(from)) && !(Line[ksq][from] & sqbb(to))) return false;
    return true;
}

bool Position::is_pseudo_legal(Move m) const {
    if (m == MOVE_NONE || m == MOVE_NULL) return false;
    int from = move_from(m), to = move_to(m), us = stm;
    int p = board[from];
    if (p == NO_PIECE || piece_color(p) != us) return false;
    if (move_type(m) != MT_NORMAL) {
        MoveList ml;
        generate(GEN_ALL, ml);
        for (int i = 0; i < ml.size; i++)
            if (ml.moves[i] == m) return true;
        return false;
    }
    if (byColor[us] & sqbb(to)) return false;
    int pt = piece_type(p);
    if (pt == PAWN) {
        if (rank_of(to) == 7 || rank_of(to) == 0) return false;
        int up = us == WHITE ? 8 : -8;
        if (board[to] != NO_PIECE) {
            if (!(PawnAtt[us][from] & sqbb(to))) return false;
        } else if (to == from + up) {
        } else if (to == from + 2 * up && rank_of(from) == (us == WHITE ? 1 : 6) &&
                   board[from + up] == NO_PIECE) {
        } else
            return false;
    } else {
        if (!(piece_attacks(pt, from, occ()) & sqbb(to))) return false;
    }
    // in double check only king moves; handled by is_legal
    return true;
}

bool Position::gives_check_approx(Move m) const {
    // direct checks + discovered checks for normal moves (not exact for special moves)
    int from = move_from(m), to = move_to(m), them = stm ^ 1;
    int eksq = king_sq(them);
    int pt = piece_type(board[from]);
    if (move_type(m) == MT_PROMO) pt = move_promo(m);
    U64 o = (occ() ^ sqbb(from)) | sqbb(to);
    if (pt == PAWN) {
        if (PawnAtt[stm][to] & sqbb(eksq)) return true;
    } else if (pt != KING) {
        if (piece_attacks(pt, to, o) & sqbb(eksq)) return true;
    }
    // discovered
    if ((rook_attacks(eksq, o) & (pieces(stm, ROOK) | pieces(stm, QUEEN)) & ~sqbb(from)) ||
        (bishop_attacks(eksq, o) & (pieces(stm, BISHOP) | pieces(stm, QUEEN)) & ~sqbb(from)))
        return true;
    return false;
}

uint64_t perft(Position &pos, int depth) {
    MoveList ml;
    pos.generate(GEN_ALL, ml);
    uint64_t n = 0;
    for (int i = 0; i < ml.size; i++) {
        Move m = ml.moves[i];
        if (!pos.is_legal(m)) continue;
        if (depth == 1) {
            n++;
            continue;
        }
        pos.make_move(m);
        n += perft(pos, depth - 1);
        pos.unmake_move(m);
    }
    return n;
}
