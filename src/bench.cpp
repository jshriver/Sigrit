#include "bench.h"
#include <cstdio>
#include <memory>
#include "syzygy.h"
#include "thread.h"
#include "tt.h"

void bench(int depth) {
    const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6",
        "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 8",
        "2r3k1/pp3ppp/1qn1p3/3pP3/3P4/P1Q2N2/1P3PPP/2R3K1 b - - 0 20",
        "6k1/5pp1/7p/8/8/7P/5PP1/3R2K1 w - - 0 1",
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "3r1rk1/p4ppp/1p2pn2/2q5/2P5/1P2Q3/P3BPPP/3R1RK1 w - - 0 18",
        "8/8/4k3/8/2p5/2P2K2/8/8 w - - 0 1",
        "r2q1rk1/ppp2ppp/2n1bn2/2b1p3/3pP3/3P1NPP/PPP1NPB1/R1BQ1RK1 b - - 0 9",
    };
    Limits lim;
    lim.depth = depth;
    tt.resize(16);
    tt.ensure();
    Worker &w = *workers[0];
    int64_t total = 0, t0 = now_ms();
    for (auto f : fens) {
        std::unique_ptr<Position> pos(new Position());
        pos->set_fen(f);
        search_clear();
        tt.new_search();
        Time.startTime = now_ms();
        Time.timed = false;
        pondering = false;
        stopFlag = false;
        tbCard = 0;
        rootInTB = false;
        w.P.copy_from(*pos);
        w.EV.attach(w.P);
        MoveList ml;
        w.rootMoves.size = 0;
        w.P.generate(GEN_ALL, ml);
        for (int i = 0; i < ml.size; i++)
            if (w.P.is_legal(ml.moves[i])) w.rootMoves.add(ml.moves[i]);
        w.iterate(lim, true);
        total += w.nodes;
    }
    int64_t el = std::max<int64_t>(1, now_ms() - t0);
    printf("bench: %lld nodes %lld ms %lld nps\n", (long long)total, (long long)el, (long long)(total * 1000 / el));
    fflush(stdout);
}
