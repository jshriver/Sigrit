#include "uci.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <sstream>
#include "bench.h"
#include "nnue.h"
#include "syzygy.h"
#include "thread.h"
#include "tt.h"

#ifndef ENGINE_VERSION
#define ENGINE_VERSION "dev"  // set by the Makefile from the git tag
#endif

static const char *ENGINE_NAME = "Sigrit " ENGINE_VERSION;
static const char *ENGINE_AUTHOR = "Joshua Shriver";

static std::mutex outMutex;
void uci_out(const std::string &s) {
    std::lock_guard<std::mutex> lk(outMutex);
    fputs(s.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static Position rootPos;
static void set_position(std::istringstream &is) {
    std::string tok, fen;
    is >> tok;
    if (tok == "startpos") {
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        is >> tok;  // maybe "moves"
    } else if (tok == "fen") {
        while (is >> tok && tok != "moves") fen += tok + " ";
    } else
        return;
    if (!rootPos.set_fen(fen)) {
        rootPos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return;
    }
    rootPos.dirtyOut = nullptr;
    while (is >> tok) {
        Move m = parse_uci_move(rootPos, tok);
        if (!m) break;
        if (rootPos.sp >= MAX_STATES - MAX_PLY - 8) {
            // compact history: keep only the last 100 plies (enough for repetition / 50-move rule)
            int keep = 100;
            for (int i = 0; i <= keep; i++) rootPos.st[i] = rootPos.st[rootPos.sp - keep + i];
            rootPos.sp = keep;
        }
        rootPos.make_move(m);
    }
}

static void go(std::istringstream &is) {
    int64_t t0 = now_ms();
    Limits lim;
    std::string tok;
    while (is >> tok) {
        if (tok == "wtime") is >> lim.wtime;
        else if (tok == "btime") is >> lim.btime;
        else if (tok == "winc") is >> lim.winc;
        else if (tok == "binc") is >> lim.binc;
        else if (tok == "movetime") is >> lim.movetime;
        else if (tok == "movestogo") is >> lim.movestogo;
        else if (tok == "depth") is >> lim.depth;
        else if (tok == "infinite") lim.infinite = true;
        else if (tok == "ponder") lim.ponder = true;
    }
    start_thinking(rootPos, lim, t0);
}

void uci_loop(int argc, char **argv) {
    rootPos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    if (argc > 1 && std::string(argv[1]) == "bench") {
        bench(argc > 2 ? atoi(argv[2]) : 12);
        return;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string cmd;
        if (!(is >> cmd)) continue;
        if (cmd == "uci") {
            uci_out(std::string("id name ") + ENGINE_NAME);
            uci_out(std::string("id author ") + ENGINE_AUTHOR);
            uci_out("option name Hash type spin default 256 min 1 max 65536");
            uci_out("option name Threads type spin default 1 min 1 max 1024");
            uci_out("option name Move Overhead type spin default 30 min 0 max 5000");
            uci_out("option name Ponder type check default false");
            uci_out("option name UCI_Chess960 type check default false");
            uci_out("option name SyzygyPath type string default <empty>");
            uci_out("option name SyzygyProbeDepth type spin default 1 min 1 max 100");
            uci_out("option name Syzygy50MoveRule type check default true");
            uci_out("option name SyzygyProbeLimit type spin default 7 min 0 max 7");
            uci_out("uciok");
        } else if (cmd == "isready") {
            if (!search_running()) tt.ensure();
            uci_out("readyok");
        } else if (cmd == "ucinewgame") {
            wait_for_search();
            tt.ensure();
            search_clear();
        } else if (cmd == "setoption") {
            wait_for_search();
            std::string tok, name, value;
            bool inName = false, inValue = false;
            while (is >> tok) {
                if (tok == "name") inName = true, inValue = false;
                else if (tok == "value") inValue = true, inName = false;
                else if (inName) name += (name.empty() ? "" : " ") + tok;
                else if (inValue) value += (value.empty() ? "" : " ") + tok;
            }
            for (auto &c : name) c = tolower(c);
            std::string lval = value;
            for (auto &c : lval) c = tolower(c);
            try {
                if (name == "hash") {
                    long mb = std::stol(value);
                    if (mb < 1) mb = 1;
                    if (mb > 65536) mb = 65536;
                    tt.resize(mb);
                } else if (name == "move overhead") {
                    moveOverhead = std::max(0, std::min(5000, std::stoi(value)));
                } else if (name == "threads") {
                    set_threads(std::max(1, std::min(1024, std::stoi(value))));
                } else if (name == "uci_chess960") {
                    Chess960 = lval == "true";
                } else if (name == "syzygypath") {
                    TbInfo tb = tb_set_path(value);
                    if (value.empty() || value == "<empty>")
                        uci_out("info string Syzygy: tablebases unloaded");
                    else if (!tb.pieces)
                        uci_out("info string Syzygy: no tablebase files found in " + value);
                    else
                        uci_out("info string Syzygy: found " + std::to_string(tb.wdl) + " WDL and " +
                                std::to_string(tb.dtz) + " DTZ files, up to " + std::to_string(tb.pieces) +
                                "-piece tablebases");
                } else if (name == "syzygyprobedepth") {
                    tbProbeDepth = std::max(1, std::min(100, std::stoi(value)));
                } else if (name == "syzygy50moverule") {
                    tb50MoveRule = lval == "true";
                } else if (name == "syzygyprobelimit") {
                    tbProbeLimit = std::max(0, std::min(7, std::stoi(value)));
                }
            } catch (...) {
            }
        } else if (cmd == "position") {
            wait_for_search();
            set_position(is);
        } else if (cmd == "go") {
            wait_for_search();
            go(is);
        } else if (cmd == "stop") {
            stop_search();
        } else if (cmd == "ponderhit") {
            ponderhit();
        } else if (cmd == "quit") {
            stop_search();
            break;
        } else if (cmd == "perft") {
            wait_for_search();
            int d = 1;
            is >> d;
            int64_t t0 = now_ms();
            uint64_t n = perft(rootPos, d);
            uci_out("nodes " + std::to_string(n) + " time " + std::to_string(now_ms() - t0));
        } else if (cmd == "d") {
            uci_out(rootPos.fen());
        } else if (cmd == "eval") {
            uci_out("eval " + std::to_string(nnue::evaluate_full(rootPos)));
        }
    }
    stop_search();
}

std::string move_to_uci(Move m) { return move_to_uci(m, Chess960); }

std::string move_to_uci(Move m, bool c960) {
    if (m == MOVE_NONE) return "0000";
    int from = move_from(m), to = move_to(m);
    if (move_type(m) == MT_CASTLE && !c960)  // standard notation: king's destination
        to = rank_of(from) * 8 + (to > from ? 6 : 2);
    std::string s;
    s += char('a' + file_of(from));
    s += char('1' + rank_of(from));
    s += char('a' + file_of(to));
    s += char('1' + rank_of(to));
    if (move_type(m) == MT_PROMO) s += "nbrq"[move_promo(m) - KNIGHT];
    return s;
}

Move parse_uci_move(Position &pos, const std::string &s) {
    MoveList ml;
    pos.generate(GEN_ALL, ml);
    for (int i = 0; i < ml.size; i++)
        if (move_to_uci(ml.moves[i]) == s && pos.is_legal(ml.moves[i])) return ml.moves[i];
    // also accept the other castling notation (king-takes-rook vs. king destination)
    for (int i = 0; i < ml.size; i++)
        if (move_type(ml.moves[i]) == MT_CASTLE && move_to_uci(ml.moves[i], !Chess960) == s &&
            pos.is_legal(ml.moves[i]))
            return ml.moves[i];
    return MOVE_NONE;
}

std::string uci_score(int s) {
    if (s >= MATE_BOUND) return "mate " + std::to_string((MATE - s + 1) / 2);
    if (s <= -MATE_BOUND) return "mate " + std::to_string(-(MATE + s) / 2);
    if (s >= TB_WIN_BOUND) return "cp " + std::to_string(20000 - (TB_WIN_SCORE - s));
    if (s <= -TB_WIN_BOUND) return "cp " + std::to_string(-20000 + (TB_WIN_SCORE + s));
    return "cp " + std::to_string(s);
}
