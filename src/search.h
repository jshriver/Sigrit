// Alpha-beta search: PVS with null move, LMR, singular extensions, probcut, TB probes.
#pragma once
#include <atomic>
#include "history.h"
#include "nnue.h"
#include "position.h"
#include "timeman.h"

// Per-thread search state (Lazy SMP: one Worker per thread, all sharing the TT).
struct Worker : Histories {
    int id;
    Position P;
    nnue::Evaluator EV;
    SStack stackArr[MAX_PLY + 16];

    std::atomic<int64_t> nodes{0}, tbHits{0};
    int seldepth = 0, rootDepth = 0;
    int64_t rootMoveNodes[4096];
    MoveList rootMoves;

    // result of the last completed iteration
    int completedDepth = 0, bestScore = 0;
    Move bestMove = MOVE_NONE;
    Move pv[MAX_PLY + 1];
    int pvLen = 0;

    explicit Worker(int i) : id(i) { clear(); }

    void inc_nodes() { nodes.store(nodes.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); }
    bool is_root_move(Move m) const {
        for (int i = 0; i < rootMoves.size; i++)
            if (rootMoves.moves[i] == m) return true;
        return false;
    }
    inline bool check_time();
    inline void do_move(Move m, SStack *ss);
    inline void undo_move(Move m) { P.unmake_move(m); }
    inline int corrected_eval(int raw, const SStack *ss) const;
    inline void update_corr(int diff, int depth, const SStack *ss);
    template <bool PV> int qsearch(int alpha, int beta, int ply);
    template <bool PV> int search(int alpha, int beta, int depth, int ply, bool cutnode);
    void iterate(const Limits &lim, bool silent);
};

void search_init();
// One "info depth ..." line for w's last completed iteration.
void print_info(const Worker &w, int depth, int seldepth, int score);
