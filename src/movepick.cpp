#include "movepick.h"
#include <algorithm>

enum {
    ST_TT, ST_CAP_GEN, ST_CAP, ST_K1, ST_K2, ST_CM, ST_Q_GEN, ST_Q, ST_BAD, ST_DONE,
    ST_QS_TT, ST_QS_GEN, ST_QS, ST_EV_TT, ST_EV_GEN, ST_EV
};

Picker::Picker(const Position &p, const Histories &hist, SStack *s, Move tm, bool qs)
    : pos(p), h(hist), ss(s), ttm(tm) {
    k1 = ss->killer[0];
    k2 = ss->killer[1];
    Move pm = (ss - 1)->move;
    cm = (pm && pm != MOVE_NULL) ? h.counterMove[(ss - 1)->piece][move_to(pm)] : MOVE_NONE;
    if (pos.in_check()) stage = ST_EV_TT;
    else stage = qs ? ST_QS_TT : ST_TT;
    if (!ttm || !pos.is_pseudo_legal(ttm) || (qs && !pos.in_check() && !pos.is_tactical(ttm))) {
        ttm = MOVE_NONE;
        stage++;
    }
}

int Picker::cap_score(Move m) const {
    int victim = move_type(m) == MT_EP ? PAWN : (pos.board[move_to(m)] == NO_PIECE ? NO_PT : piece_type(pos.board[move_to(m)]));
    int vt = victim == NO_PT ? 0 : victim;
    int s = PieceVal[victim == NO_PT ? 6 : victim] * 16;
    if (move_type(m) == MT_PROMO) s += 8000;
    s += h.capHist[pos.board[move_from(m)]][move_to(m)][vt];
    return s;
}

int Picker::quiet_score(Move m) const {
    int pc = pos.board[move_from(m)], to = move_to(m);
    int s = h.mainHist[pos.stm][m & 4095];
    s += h.pawnHist[pos.s().pawnKey & (PH_SIZE - 1)][pc][to];
    s += (ss - 1)->ch[pc][to];
    s += (ss - 2)->ch[pc][to];
    s += (ss - 4)->ch[pc][to] / 2;
    return s;
}

void Picker::score_caps() {
    for (int i = 0; i < list.size; i++) scores[i] = cap_score(list.moves[i]);
}

void Picker::score_quiets() {
    int them = pos.stm ^ 1;
    U64 occ = pos.occ();
    U64 tp = 0, tm, tr;
    U64 ep = pos.pieces(them, PAWN);
    tp = them == WHITE ? (((ep & ~FILE_A) << 7) | ((ep & ~(FILE_A << 7)) << 9))
                       : (((ep & ~FILE_A) >> 9) | ((ep & ~(FILE_A << 7)) >> 7));
    tm = tp;
    U64 b = pos.pieces(them, KNIGHT);
    while (b) tm |= KnightAtt[pop_lsb(b)];
    b = pos.pieces(them, BISHOP);
    while (b) tm |= bishop_attacks(pop_lsb(b), occ);
    tr = tm;
    b = pos.pieces(them, ROOK);
    while (b) tr |= rook_attacks(pop_lsb(b), occ);
    for (int i = 0; i < list.size; i++) {
        Move m = list.moves[i];
        int s = quiet_score(m);
        int pt = piece_type(pos.board[move_from(m)]);
        U64 f = sqbb(move_from(m)), t = sqbb(move_to(m));
        if (pt == QUEEN) s += (f & tr) ? 16000 : 0, s -= (t & tr) ? 14000 : 0;
        else if (pt == ROOK) s += (f & tm) ? 12000 : 0, s -= (t & tm) ? 10000 : 0;
        else if (pt == KNIGHT || pt == BISHOP) s += (f & tp) ? 8000 : 0, s -= (t & tp) ? 7000 : 0;
        scores[i] = s;
    }
}

Move Picker::pick_best() {
    int b = idx;
    for (int i = idx + 1; i < list.size; i++)
        if (scores[i] > scores[b]) b = i;
    std::swap(list.moves[b], list.moves[idx]);
    std::swap(scores[b], scores[idx]);
    return list.moves[idx++];
}

bool Picker::is_quiet_ok(Move m) const {
    return m && m != ttm && !pos.is_tactical(m) && pos.is_pseudo_legal(m);
}

Move Picker::next() {
    switch (stage) {
    case ST_TT:
    case ST_QS_TT:
    case ST_EV_TT:
        stage++;
        return ttm;
    case ST_CAP_GEN:
        list.size = 0;
        pos.generate(GEN_CAPTURES, list);
        score_caps();
        idx = 0;
        stage = ST_CAP;
        [[fallthrough]];
    case ST_CAP:
        while (idx < list.size) {
            Move m = pick_best();
            if (m == ttm) continue;
            if (!pos.see_ge(m, -scores[idx - 1] / 32)) {
                bad[nbad++] = m;
                continue;
            }
            return m;
        }
        stage = ST_K1;
        [[fallthrough]];
    case ST_K1:
        stage = ST_K2;
        if (!skipQuiets && is_quiet_ok(k1)) return k1;
        [[fallthrough]];
    case ST_K2:
        stage = ST_CM;
        if (!skipQuiets && k2 != k1 && is_quiet_ok(k2)) return k2;
        [[fallthrough]];
    case ST_CM:
        stage = ST_Q_GEN;
        if (!skipQuiets && cm != k1 && cm != k2 && is_quiet_ok(cm)) return cm;
        [[fallthrough]];
    case ST_Q_GEN:
        if (!skipQuiets) {
            list.size = 0;
            pos.generate(GEN_QUIETS, list);
            score_quiets();
            idx = 0;
        } else
            list.size = idx = 0;
        stage = ST_Q;
        [[fallthrough]];
    case ST_Q:
        while (!skipQuiets && idx < list.size) {
            Move m = pick_best();
            if (m == ttm || m == k1 || m == k2 || m == cm) continue;
            return m;
        }
        stage = ST_BAD;
        [[fallthrough]];
    case ST_BAD:
        while (badIdx < nbad) {
            Move m = bad[badIdx++];
            if (m != ttm) return m;
        }
        stage = ST_DONE;
        return MOVE_NONE;
    case ST_QS_GEN:
        list.size = 0;
        pos.generate(GEN_CAPTURES, list);
        score_caps();
        idx = 0;
        stage = ST_QS;
        [[fallthrough]];
    case ST_QS:
        while (idx < list.size) {
            Move m = pick_best();
            if (m == ttm) continue;
            return m;
        }
        stage = ST_DONE;
        return MOVE_NONE;
    case ST_EV_GEN:
        list.size = 0;
        pos.generate(GEN_ALL, list);
        for (int i = 0; i < list.size; i++) {
            Move m = list.moves[i];
            scores[i] = pos.is_capture(m) ? 1000000 + cap_score(m) : quiet_score(m);
        }
        idx = 0;
        stage = ST_EV;
        [[fallthrough]];
    case ST_EV:
        while (idx < list.size) {
            Move m = pick_best();
            if (m == ttm) continue;
            return m;
        }
        stage = ST_DONE;
        return MOVE_NONE;
    default:
        return MOVE_NONE;
    }
}
