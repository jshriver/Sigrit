#include "search.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "movepick.h"
#include "syzygy.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

int16_t dummyCH[12][64];
static int lmrTable[64][64];

void search_init() {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++)
            lmrTable[d][m] = (d && m) ? int(1.1 + std::log(d) * std::log(m) / 1.85) : 0;
}

static inline int score_to_tt(int s, int ply) {
    if (s >= TB_WIN_BOUND) return s + ply;
    if (s <= -TB_WIN_BOUND) return s - ply;
    return s;
}
static inline int score_from_tt(int s, int ply) {
    if (s == VALUE_NONE) return s;
    if (s >= TB_WIN_BOUND) return s - ply;
    if (s <= -TB_WIN_BOUND) return s + ply;
    return s;
}

static inline void hist_update(int16_t &h, int bonus) {
    bonus = std::clamp(bonus, -16000, 16000);
    h += bonus - h * std::abs(bonus) / 16384;
}

static inline int stat_bonus(int d) { return std::min(170 * d - 80, 1700); }

inline bool Worker::check_time() {
    if (stopFlag.load(std::memory_order_relaxed)) return true;
    if (id == 0 && (nodes.load(std::memory_order_relaxed) & 1023) == 0 && Time.timed && rootDepth > 1 &&
        !pondering.load(std::memory_order_relaxed) && Time.elapsed() >= Time.hardLimit)
        stopFlag = true;
    return stopFlag.load(std::memory_order_relaxed);
}

inline void Worker::do_move(Move m, SStack *ss) {
    ss->move = m;
    ss->piece = P.board[move_from(m)];
    ss->ch = &contHist[ss->piece][move_to(m)][0];
    P.make_move(m);
    __builtin_prefetch(tt.bucket(P.s().key));
    EV.invalidate(P.sp);
    inc_nodes();
}

inline int Worker::corrected_eval(int raw, const SStack *ss) const {
    const StateInfo &x = P.s();
    int c = (2 * corrHist[P.stm][x.pawnKey & (CORR_SIZE - 1)] + npCorr[P.stm][0][x.npKey[0] & (CORR_SIZE - 1)] +
             npCorr[P.stm][1][x.npKey[1] & (CORR_SIZE - 1)]) /
            (3 * CORR_GRAIN);
    Move pm = (ss - 1)->move;
    if (pm && pm != MOVE_NULL) c += contCorr[P.stm][(ss - 1)->piece][move_to(pm)] / (2 * CORR_GRAIN);
    int v = raw * (200 - P.s().halfmove) / 200 + c;
    return std::clamp(v, -TB_WIN_BOUND + 1, TB_WIN_BOUND - 1);
}

static inline void corr_entry(int &e, int diff, int w) {
    e = (e * (256 - w) + diff * CORR_GRAIN * w) / 256;
    e = std::clamp(e, -64 * CORR_GRAIN, 64 * CORR_GRAIN);
}
inline void Worker::update_corr(int diff, int depth, const SStack *ss) {
    const StateInfo &x = P.s();
    int w = std::min(depth + 1, 16);
    corr_entry(corrHist[P.stm][x.pawnKey & (CORR_SIZE - 1)], diff, w);
    corr_entry(npCorr[P.stm][0][x.npKey[0] & (CORR_SIZE - 1)], diff, w);
    corr_entry(npCorr[P.stm][1][x.npKey[1] & (CORR_SIZE - 1)], diff, w);
    Move pm = (ss - 1)->move;
    if (pm && pm != MOVE_NULL) corr_entry(contCorr[P.stm][(ss - 1)->piece][move_to(pm)], diff, w);
}

// ---------------------------------------------------------------- qsearch
template <bool PV>
int Worker::qsearch(int alpha, int beta, int ply) {
    SStack *ss = &stackArr[ply + 4];
    if (PV) ss->pvLen = 0;
    if (check_time()) return 0;
    if (ply > seldepth) seldepth = ply;
    if (P.is_draw(ply)) return 0;
    bool inCheck = P.in_check();
    if (ply >= MAX_PLY - 1) return inCheck ? 0 : corrected_eval(EV.evaluate(P), ss);

    bool hit;
    TTEntry *te = tt.probe(P.s().key, hit);
    int ttScore = hit ? score_from_tt(te->score, ply) : VALUE_NONE;
    Move ttMove = hit ? te->move : MOVE_NONE;
    int ttBound = hit ? (te->genBound & 3) : BOUND_NONE;
    if (!PV && hit && ttScore != VALUE_NONE &&
        ((ttBound == BOUND_EXACT) || (ttBound == BOUND_LOWER && ttScore >= beta) ||
         (ttBound == BOUND_UPPER && ttScore <= alpha)))
        return ttScore;

    int bestScore, rawEval = VALUE_NONE, futBase;
    if (inCheck) {
        bestScore = -INF;
        futBase = -INF;
        ss->eval = VALUE_NONE;
    } else {
        rawEval = hit && te->eval != VALUE_NONE ? te->eval : EV.evaluate(P);
        bestScore = ss->eval = corrected_eval(rawEval, ss);
        if (hit && ttScore != VALUE_NONE &&
            ((ttBound == BOUND_LOWER && ttScore > bestScore) || (ttBound == BOUND_UPPER && ttScore < bestScore)))
            bestScore = ttScore;
        if (bestScore >= beta) {
            if (!hit) tt.store(te, P.s().key, MOVE_NONE, score_to_tt(bestScore, ply), rawEval, 0, BOUND_LOWER);
            return bestScore;
        }
        if (bestScore > alpha) alpha = bestScore;
        futBase = bestScore + 160;
    }

    Picker mp(P, *this, ss, ttMove, true);
    Move bestMove = MOVE_NONE, m;
    int moveCount = 0;
    int oldAlpha = alpha;
    while ((m = mp.next())) {
        if (!P.is_legal(m)) continue;
        moveCount++;
        if (bestScore > -TB_WIN_BOUND) {
            if (!inCheck && !(move_type(m) == MT_PROMO)) {
                if (futBase > -INF) {
                    int victim = move_type(m) == MT_EP ? PAWN : piece_type(P.board[move_to(m)]);
                    if (futBase + PieceVal[victim] <= alpha && !P.gives_check_approx(m)) {
                        bestScore = std::max(bestScore, futBase + PieceVal[victim]);
                        continue;
                    }
                }
            }
            if (inCheck && moveCount > 2 && !P.is_capture(m)) continue;  // limited quiet evasions
            if (!P.see_ge(m, -50)) continue;
        }
        do_move(m, ss);
        int score = -qsearch<PV>(-beta, -alpha, ply + 1);
        undo_move(m);
        if (stopFlag.load(std::memory_order_relaxed)) return 0;
        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                if (PV) {
                    SStack *ch = ss + 1;
                    ss->pv[0] = m;
                    memcpy(ss->pv + 1, ch->pv, ch->pvLen * sizeof(Move));
                    ss->pvLen = ch->pvLen + 1;
                }
                if (score >= beta) break;
            }
        }
    }
    if (inCheck && bestScore == -INF) return -MATE + ply;
    int bound = bestScore >= beta ? BOUND_LOWER : (PV && bestScore > oldAlpha) ? BOUND_EXACT : BOUND_UPPER;
    tt.store(te, P.s().key, bestMove, score_to_tt(bestScore, ply), rawEval, 0, bound);
    return bestScore;
}

// ---------------------------------------------------------------- main search
template <bool PV>
int Worker::search(int alpha, int beta, int depth, int ply, bool cutnode) {
    SStack *ss = &stackArr[ply + 4];
    bool root = ply == 0;
    if (PV) ss->pvLen = 0;
    if (depth <= 0) return qsearch<PV>(alpha, beta, ply);
    if (check_time()) return 0;
    if (PV && ply > seldepth) seldepth = ply;
    bool inCheck = P.in_check();

    if (!root) {
        if (P.is_draw(ply)) return 0;
        if (alpha < 0 && P.upcoming_repetition(ply)) {
            alpha = 0;
            if (alpha >= beta) return alpha;
        }
        if (ply >= MAX_PLY - 1) return inCheck ? 0 : corrected_eval(EV.evaluate(P), ss);
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    Move excluded = ss->excluded;
    bool hit = false;
    TTEntry *te = tt.probe(P.s().key, hit);
    if (excluded) hit = false;
    int ttScore = hit ? score_from_tt(te->score, ply) : VALUE_NONE;
    Move ttMove = root ? ss->pv[0] : hit ? te->move : MOVE_NONE;
    if (root && !ttMove && hit) ttMove = te->move;
    int ttBound = hit ? (te->genBound & 3) : BOUND_NONE;
    int ttDepth = hit ? te->depth : 0;
    bool ttPv = PV || (hit && (te->genBound & 4));
    bool ttCapture = ttMove && P.is_tactical(ttMove);

    if (!PV && hit && ttDepth >= depth && ttScore != VALUE_NONE && P.s().halfmove < 90 &&
        ((ttBound == BOUND_EXACT) || (ttBound == BOUND_LOWER && ttScore >= beta) ||
         (ttBound == BOUND_UPPER && ttScore <= alpha))) {
        // reward quiet tt move on fail high
        if (ttMove && ttScore >= beta && !ttCapture && P.is_pseudo_legal(ttMove)) {
            hist_update(mainHist[P.stm][ttMove & 4095], stat_bonus(depth));
        }
        return ttScore;
    }

    int bestScore = -INF, maxScore = INF;

    // Syzygy WDL probe
    if (!root && !excluded && tbCard) {
        int pc = popcount(P.occ());
        if (pc <= tbCard && (pc < tbCard || depth >= tbProbeDepth) && P.s().halfmove == 0 && !P.s().castling) {
            int wdl;
            if (tb_probe(P, wdl)) {
                tbHits.store(tbHits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                int drawScore = tb50MoveRule ? 1 : 0;
                int v = wdl < -drawScore ? -TB_WIN_SCORE + ply : wdl > drawScore ? TB_WIN_SCORE - ply : 2 * wdl * drawScore;
                int b = wdl < -drawScore ? BOUND_UPPER : wdl > drawScore ? BOUND_LOWER : BOUND_EXACT;
                if (b == BOUND_EXACT || (b == BOUND_LOWER ? v >= beta : v <= alpha)) {
                    tt.store(te, P.s().key, MOVE_NONE, score_to_tt(v, ply), VALUE_NONE,
                             std::min(MAX_PLY - 1, depth + 6), b, ttPv);
                    return v;
                }
                if (PV) {
                    if (b == BOUND_LOWER) bestScore = v, alpha = std::max(alpha, v);
                    else maxScore = v;
                }
            }
        }
    }

    int rawEval = VALUE_NONE, eval;
    bool improving = false;
    (ss + 1)->killer[0] = (ss + 1)->killer[1] = MOVE_NONE;
    (ss + 1)->excluded = MOVE_NONE;
    (ss + 1)->cutoffCnt = 0;
    if (inCheck) {
        ss->eval = eval = VALUE_NONE;
    } else if (excluded) {
        eval = ss->eval;
        rawEval = eval;
    } else {
        rawEval = hit && te->eval != VALUE_NONE ? te->eval : EV.evaluate(P);
        ss->eval = eval = corrected_eval(rawEval, ss);
        if (hit && ttScore != VALUE_NONE &&
            ((ttBound == BOUND_LOWER && ttScore > eval) || (ttBound == BOUND_UPPER && ttScore < eval)))
            eval = ttScore;
        if (!hit) tt.store(te, P.s().key, MOVE_NONE, VALUE_NONE, rawEval, -1, BOUND_NONE, ttPv);
    }
    if (!inCheck) {
        if ((ss - 2)->eval != VALUE_NONE) improving = ss->eval > (ss - 2)->eval;
        else if ((ss - 4)->eval != VALUE_NONE) improving = ss->eval > (ss - 4)->eval;
        else improving = true;
        // opponent's quiet move: reward/punish by static eval swing
        Move pm = (ss - 1)->move;
        if (pm && pm != MOVE_NULL && (ss - 1)->eval != VALUE_NONE && !excluded && P.s().captured == NO_PIECE &&
            move_type(pm) != MT_PROMO) {
            int bonus = std::clamp(-10 * ((ss - 1)->eval + ss->eval), -1000, 1000);
            hist_update(mainHist[P.stm ^ 1][pm & 4095], bonus);
        }
    }

    if (!PV && !inCheck && !excluded) {
        // reverse futility
        if (depth <= 8 && eval - 80 * (depth - improving) >= beta && eval < TB_WIN_BOUND) return (eval + beta) / 2;
        // razoring
        if (depth <= 3 && eval + 250 * depth < alpha) {
            int s = qsearch<false>(alpha, alpha + 1, ply);
            if (s <= alpha) return s;
        }
        // null move
        if (depth >= 3 && eval >= beta && ss->eval >= beta - 20 * depth + 160 && (ss - 1)->move != MOVE_NULL &&
            P.has_non_pawn(P.stm) && beta > -TB_WIN_BOUND) {
            int R = 5 + depth / 3 + std::min((eval - beta) / 200, 3);
            ss->move = MOVE_NULL;
            ss->piece = 0;
            ss->ch = dummyCH;
            P.make_null();
            EV.invalidate(P.sp);
            inc_nodes();
            int s = -search<false>(-beta, -beta + 1, depth - R, ply + 1, !cutnode);
            P.unmake_null();
            if (stopFlag.load(std::memory_order_relaxed)) return 0;
            if (s >= beta) return s >= TB_WIN_BOUND ? beta : s;
        }
        // probcut
        int pcBeta = beta + 200;
        if (depth >= 5 && std::abs(beta) < TB_WIN_BOUND &&
            !(hit && ttDepth >= depth - 3 && ttScore != VALUE_NONE && ttScore < pcBeta)) {
            Picker mp(P, *this, ss, ttCapture ? ttMove : MOVE_NONE, true);
            Move m;
            while ((m = mp.next())) {
                if (!P.is_legal(m) || !P.see_ge(m, pcBeta - ss->eval)) continue;
                do_move(m, ss);
                int s = -qsearch<false>(-pcBeta, -pcBeta + 1, ply + 1);
                if (s >= pcBeta) s = -search<false>(-pcBeta, -pcBeta + 1, depth - 4, ply + 1, !cutnode);
                undo_move(m);
                if (stopFlag.load(std::memory_order_relaxed)) return 0;
                if (s >= pcBeta) {
                    tt.store(te, P.s().key, m, score_to_tt(s, ply), rawEval, depth - 3, BOUND_LOWER, ttPv);
                    return s;
                }
            }
        }
    }

    // internal iterative reduction
    if ((PV || cutnode) && depth >= 4 && !ttMove) depth--;

    Picker mp(P, *this, ss, ttMove, false);
    Move m, bestMove = MOVE_NONE;
    int moveCount = 0;
    Move quiets[64], caps[32];
    int nq = 0, nc = 0;
    int64_t nodesBefore = 0;

    while ((m = mp.next())) {
        if (m == excluded) continue;
        if (root && !is_root_move(m)) continue;
        if (!P.is_legal(m)) continue;
        moveCount++;
        bool quiet = !P.is_tactical(m);
        int hist = quiet ? mainHist[P.stm][m & 4095] + (ss - 1)->ch[P.board[move_from(m)]][move_to(m)] +
                               (ss - 2)->ch[P.board[move_from(m)]][move_to(m)]
                         : capHist[P.board[move_from(m)]][move_to(m)][P.board[move_to(m)] == NO_PIECE ? 0 : piece_type(P.board[move_to(m)])];

        if (!root && bestScore > -TB_WIN_BOUND && P.has_non_pawn(P.stm)) {
            int lmrDepth = std::max(0, depth - 1 - lmrTable[std::min(depth, 63)][std::min(moveCount, 63)]);
            if (quiet) {
                if (moveCount >= (3 + depth * depth) / (2 - improving)) mp.skipQuiets = true;
                if (!inCheck && lmrDepth <= 8 && ss->eval + 100 + 110 * lmrDepth <= alpha) {
                    mp.skipQuiets = true;
                    continue;
                }
                if (lmrDepth <= 4 && hist < -3000 * depth) continue;
                if (!P.see_ge(m, -30 * lmrDepth * lmrDepth)) continue;
            } else {
                if (!inCheck && lmrDepth < 7 && move_type(m) != MT_PROMO) {
                    int victim = move_type(m) == MT_EP ? PAWN : piece_type(P.board[move_to(m)]);
                    if (ss->eval + 250 + 180 * lmrDepth + PieceVal[victim] + hist / 8 <= alpha) continue;
                }
                if (!P.see_ge(m, -100 * depth)) continue;
            }
        }

        // singular extension
        int ext = 0;
        if (!root && depth >= 6 && m == ttMove && !excluded && ttDepth >= depth - 3 && (ttBound & BOUND_LOWER) &&
            std::abs(ttScore) < TB_WIN_BOUND && ply < 2 * rootDepth) {
            int sBeta = ttScore - depth;
            ss->excluded = m;
            int s = search<false>(sBeta - 1, sBeta, (depth - 1) / 2, ply, cutnode);
            ss->excluded = MOVE_NONE;
            if (stopFlag.load(std::memory_order_relaxed)) return 0;
            if (s < sBeta) {
                ext = 1;
                if (!PV && s < sBeta - 20 && ss->doubleExt <= 6) ext = 2;
            } else if (sBeta >= beta)
                return sBeta;
            else if (ttScore >= beta)
                ext = -1;
            else if (cutnode)
                ext = -1;
        }
        (ss + 1)->doubleExt = ss->doubleExt + (ext >= 2);

        if (root) nodesBefore = nodes.load(std::memory_order_relaxed);
        do_move(m, ss);
        int newDepth = depth - 1 + ext;
        int score;
        // late move reductions
        if (depth >= 2 && moveCount > 1 + root && (quiet || !PV)) {
            int r = lmrTable[std::min(depth, 63)][std::min(moveCount, 63)];
            r += !PV;
            r += cutnode;
            r += !improving;
            r += ttCapture && quiet;
            r -= ttPv;
            r += (ss + 1)->cutoffCnt > 3;
            r -= P.in_check();
            r -= hist / 8000;
            r -= (m == mp.k1 || m == mp.k2 || m == mp.cm);
            if (!quiet) r = std::max(0, r - 1);
            int d = std::clamp(newDepth - r, 1, newDepth + 1);
            score = -search<false>(-alpha - 1, -alpha, d, ply + 1, true);
            if (score > alpha && d < newDepth) {
                bool deeper = score > bestScore + 50 + 2 * newDepth;
                bool shallower = score < bestScore + 8;
                newDepth += deeper - shallower;
                if (d < newDepth) score = -search<false>(-alpha - 1, -alpha, newDepth, ply + 1, !cutnode);
            }
        } else if (!PV || moveCount > 1) {
            score = -search<false>(-alpha - 1, -alpha, newDepth, ply + 1, !cutnode);
        }
        if (PV && (moveCount == 1 || score > alpha)) score = -search<true>(-beta, -alpha, newDepth, ply + 1, false);
        undo_move(m);
        if (stopFlag.load(std::memory_order_relaxed)) return 0;
        if (root) rootMoveNodes[m & 4095] += nodes.load(std::memory_order_relaxed) - nodesBefore;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                if (PV) {
                    SStack *ch = ss + 1;
                    ss->pv[0] = m;
                    memcpy(ss->pv + 1, ch->pv, ch->pvLen * sizeof(Move));
                    ss->pvLen = ch->pvLen + 1;
                }
                if (score >= beta) {
                    ss->cutoffCnt++;
                    break;
                }
                alpha = score;
            }
        }
        if (m != bestMove) {
            if (quiet && nq < 64) quiets[nq++] = m;
            else if (!quiet && nc < 32) caps[nc++] = m;
        }
    }

    if (!moveCount) return excluded ? alpha : inCheck ? -MATE + ply : 0;

    if (bestScore >= beta && bestMove) {
        int bonus = stat_bonus(depth), malus = stat_bonus(depth);
        if (!P.is_tactical(bestMove)) {
            if (ss->killer[0] != bestMove) {
                ss->killer[1] = ss->killer[0];
                ss->killer[0] = bestMove;
            }
            Move pm = (ss - 1)->move;
            if (pm && pm != MOVE_NULL) counterMove[(ss - 1)->piece][move_to(pm)] = bestMove;
            auto upd = [&](Move mv, int b) {
                int pc = P.board[move_from(mv)], to = move_to(mv);
                hist_update(mainHist[P.stm][mv & 4095], b);
                hist_update(pawnHist[P.s().pawnKey & (PH_SIZE - 1)][pc][to], b);
                if ((ss - 1)->ch != dummyCH) hist_update((ss - 1)->ch[pc][to], b);
                if ((ss - 2)->ch != dummyCH) hist_update((ss - 2)->ch[pc][to], b);
                if ((ss - 4)->ch != dummyCH) hist_update((ss - 4)->ch[pc][to], b / 2);
            };
            upd(bestMove, bonus);
            for (int i = 0; i < nq; i++) upd(quiets[i], -malus);
        } else {
            int victim = P.board[move_to(bestMove)] == NO_PIECE ? 0 : piece_type(P.board[move_to(bestMove)]);
            hist_update(capHist[P.board[move_from(bestMove)]][move_to(bestMove)][victim], bonus);
        }
        for (int i = 0; i < nc; i++) {
            Move c = caps[i];
            int victim = P.board[move_to(c)] == NO_PIECE ? 0 : piece_type(P.board[move_to(c)]);
            hist_update(capHist[P.board[move_from(c)]][move_to(c)][victim], -malus);
        }
    }

    if (PV) bestScore = std::min(bestScore, maxScore);

    if (!excluded) {
        int bound = bestScore >= beta ? BOUND_LOWER : (PV && bestMove) ? BOUND_EXACT : BOUND_UPPER;
        if (!inCheck && (!bestMove || !P.is_tactical(bestMove)) &&
            !(bound == BOUND_LOWER && bestScore <= ss->eval) && !(bound == BOUND_UPPER && bestScore >= ss->eval))
            update_corr(bestScore - ss->eval, depth, ss);
        tt.store(te, P.s().key, bestMove, score_to_tt(bestScore, ply), rawEval, depth, bound, ttPv);
    }
    return bestScore;
}

void print_info(const Worker &w, int depth, int seldepth, int score) {
    int64_t el = Time.elapsed(), n = total_nodes();
    if (rootInTB && std::abs(score) < MATE_BOUND) score = rootTBScore;
    std::string s = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(seldepth) + " score " +
                    uci_score(score) + " nodes " + std::to_string(n) + " nps " +
                    std::to_string(n * 1000 / std::max<int64_t>(1, el)) + " time " + std::to_string(el) +
                    " hashfull " + std::to_string(tt.hashfull()) + " tbhits " + std::to_string(total_tbhits()) +
                    " pv";
    for (int i = 0; i < w.pvLen; i++) s += " " + move_to_uci(w.pv[i]);
    uci_out(s);
}

// Iterative deepening. Thread 0 owns time management and output; helpers just search and
// share what they find through the TT.
void Worker::iterate(const Limits &lim, bool silent) {
    bool mainThread = id == 0;
    for (int i = 0; i < MAX_PLY + 16; i++) {
        stackArr[i].killer[0] = stackArr[i].killer[1] = MOVE_NONE;
        stackArr[i].excluded = MOVE_NONE;
        stackArr[i].eval = VALUE_NONE;
        stackArr[i].move = MOVE_NONE;
        stackArr[i].ch = dummyCH;
        stackArr[i].doubleExt = 0;
        stackArr[i].pvLen = 0;
    }
    memset(rootMoveNodes, 0, sizeof(rootMoveNodes));
    EV.reset(P);
    nodes = 0;
    tbHits = 0;
    completedDepth = 0;
    bestScore = -INF;
    pvLen = 0;
    bestMove = rootMoves.size ? rootMoves.moves[0] : MOVE_NONE;
    if (!bestMove) return;

    SStack *rs = &stackArr[4];
    int prevScore = 0, score = 0;
    int maxDepth = lim.depth > 0 ? std::min(lim.depth, MAX_PLY - 1) : MAX_PLY - 1;
    int stable = 0;
    int lastScores[MAX_PLY];

    for (rootDepth = 1; rootDepth <= maxDepth; rootDepth++) {
        seldepth = 0;
        int delta = 12, alpha = -INF, beta = INF;
        if (rootDepth >= 4) {
            alpha = std::max(prevScore - delta, -INF);
            beta = std::min(prevScore + delta, INF);
        }
        int failHighReduce = 0;
        while (true) {
            int d = std::max(1, rootDepth - failHighReduce);
            score = search<true>(alpha, beta, d, 0, false);
            if (stopFlag) break;
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(score - delta, -INF);
                failHighReduce = 0;
            } else if (score >= beta) {
                beta = std::min(score + delta, INF);
                if (std::abs(score) < TB_WIN_BOUND) failHighReduce = std::min(failHighReduce + 1, 3);
            } else
                break;
            delta += delta / 2;
            if (delta > 1000) alpha = -INF, beta = INF;
        }
        if (stopFlag && rootDepth > 1) break;
        // completed iteration
        Move nb = rs->pvLen ? rs->pv[0] : bestMove;
        if (nb == bestMove) stable++;
        else stable = 0;
        bestMove = nb;
        pvLen = rs->pvLen;
        memcpy(pv, rs->pv, pvLen * sizeof(Move));
        bestScore = score;
        completedDepth = rootDepth;
        lastScores[rootDepth] = score;
        prevScore = score;
        if (!mainThread) {
            if (stopFlag) break;
            continue;
        }
        int64_t el = Time.elapsed();
        if (!silent) print_info(*this, rootDepth, seldepth, score);
        if (stopFlag) break;
        bool done = false;
        if (std::abs(score) >= MATE_BOUND && rootDepth > 10 && Time.timed && MATE - std::abs(score) < rootDepth)
            done = true;
        else if (Time.timed) {
            if (rootMoves.size == 1 && rootDepth >= 4) done = true;
            else {
                double stabF = stable >= 6 ? 0.65 : stable >= 4 ? 0.8 : stable >= 2 ? 0.95 : stable >= 1 ? 1.15 : 1.4;
                double scoreF = 1.0;
                if (rootDepth >= 4) {
                    int drop = lastScores[rootDepth - 3] - score;
                    scoreF = std::clamp(1.0 + drop / 120.0, 0.85, 1.6);
                }
                int64_t n = nodes.load(std::memory_order_relaxed);
                double frac = n ? double(rootMoveNodes[bestMove & 4095]) / double(n) : 0.5;
                double nodeF = std::clamp((1.6 - frac) * 1.1, 0.6, 1.8);
                double opt = Time.softLimit * stabF * scoreF * nodeF;
                if (el >= std::min<double>(opt, Time.hardLimit * 0.9)) done = true;
            }
        }
        if (done) {
            // while pondering, keep searching; the answer goes out as soon as ponderhit arrives
            if (!pondering) break;
            stopOnPonderhit = true;
        }
    }
}
