#include "thread.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include "syzygy.h"
#include "tt.h"
#include "uci.h"

std::vector<std::unique_ptr<Worker>> workers;
std::atomic<bool> stopFlag(false), pondering(false), stopOnPonderhit(false);
static std::thread searchThread;
static bool searching = false;

int64_t total_nodes() {
    int64_t n = 0;
    for (auto &w : workers) n += w->nodes.load(std::memory_order_relaxed);
    return n;
}
int64_t total_tbhits() {
    int64_t n = 0;
    for (auto &w : workers) n += w->tbHits.load(std::memory_order_relaxed);
    return n;
}

void set_threads(int n) {
    n = std::max(1, n);
    if (n == int(workers.size())) return;
    workers.clear();
    for (int i = 0; i < n; i++) workers.emplace_back(new Worker(i));
}

void search_clear() {
    tt.clear();
    for (auto &w : workers) w->clear();
}

// Lazy SMP thread pick: vote by score and depth, prefer proven results.
static Worker *pick_best_worker() {
    Worker *best = workers[0].get();
    if (workers.size() == 1) return best;
    int minScore = INF;
    for (auto &w : workers)
        if (w->completedDepth) minScore = std::min(minScore, w->bestScore);
    auto votes = [&](Move m) {
        int64_t v = 0;
        for (auto &u : workers)
            if (u->completedDepth && u->bestMove == m) v += int64_t(u->bestScore - minScore + 14) * u->completedDepth;
        return v;
    };
    for (auto &w : workers) {
        if (!w->completedDepth || w.get() == best) continue;
        if (std::abs(best->bestScore) >= TB_WIN_BOUND) {
            if (w->bestScore > best->bestScore) best = w.get();
        } else if (w->bestScore >= TB_WIN_BOUND || votes(w->bestMove) > votes(best->bestMove))
            best = w.get();
    }
    return best;
}

static void search_main(Position &pos, const Limits &lim, int64_t t0) {
    Time.init(lim, pos.stm, t0);

    Worker &mainW = *workers[0];
    mainW.P.copy_from(pos);
    MoveList legal, ml;
    mainW.P.generate(GEN_ALL, ml);
    for (int i = 0; i < ml.size; i++)
        if (mainW.P.is_legal(ml.moves[i])) legal.add(ml.moves[i]);
    tt.ensure();
    tt.new_search();
    tb_rank_root(mainW.P, legal);

    for (auto &w : workers) {
        if (w.get() != &mainW) w->P.copy_from(pos);
        w->EV.attach(w->P);
        w->rootMoves = legal;
        w->nodes = 0;
        w->tbHits = 0;
    }
    std::vector<std::thread> helpers;
    for (size_t i = 1; i < workers.size(); i++)
        helpers.emplace_back([i, &lim]() { workers[i]->iterate(lim, true); });

    mainW.iterate(lim, false);
    // in infinite / ponder mode, wait for stop (or ponderhit) before answering
    while (!stopFlag && (lim.infinite || pondering)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stopFlag = true;
    for (auto &t : helpers) t.join();

    Worker *best = pick_best_worker();
    if (best != &mainW && best->pvLen) print_info(*best, best->completedDepth, mainW.seldepth, best->bestScore);
    Move bm = best->bestMove;
    Move pm = best->pvLen >= 2 && best->pv[0] == bm ? best->pv[1] : MOVE_NONE;
    if (bm && !pm) {
        // ponder move from the TT when the PV got cut short
        Position &p = mainW.P;
        p.make_move(bm);
        bool hit;
        TTEntry *te = tt.probe(p.s().key, hit);
        if (hit && p.is_pseudo_legal(te->move) && p.is_legal(te->move)) pm = te->move;
        p.unmake_move(bm);
    }
    std::string out = "bestmove " + move_to_uci(bm);
    if (pm) out += " ponder " + move_to_uci(pm);
    uci_out(out);
}

void start_thinking(Position &pos, const Limits &lim, int64_t t0) {
    wait_for_search();
    // set the flags here, not in the search thread: a ponderhit sent right after "go ponder"
    // must not be overwritten when the thread starts
    stopFlag = false;
    stopOnPonderhit = false;
    pondering = lim.ponder;
    searching = true;
    searchThread = std::thread([&pos, lim, t0]() { search_main(pos, lim, t0); });
}

void wait_for_search() {
    if (searchThread.joinable()) searchThread.join();
    searching = false;
}

void stop_search() {
    stopFlag = true;
    wait_for_search();
}

bool search_running() { return searching; }

void ponderhit() {
    pondering = false;
    if (stopOnPonderhit) stopFlag = true;
}
