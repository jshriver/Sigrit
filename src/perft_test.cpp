#include "position.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <chrono>
static Position pos;
int main(int argc, char **argv) {
    init_attacks(); init_zobrist();
    int maxd = argc > 2 ? atoi(argv[2]) : 5;
    std::ifstream f(argv[1]); std::string line; int fails = 0, n = 0;
    auto t0 = std::chrono::steady_clock::now(); uint64_t total = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto p = line.find(';'); std::string fen = line.substr(0, p);
        pos.set_fen(fen); n++;
        std::stringstream ss(line.substr(p)); std::string tok;
        while (std::getline(ss, tok, ';')) {
            if (tok.size() < 3 || tok[0] != 'D') continue;
            int d = atoi(tok.c_str() + 1); uint64_t exp = strtoull(tok.c_str() + 3, 0, 10);
            if (d > maxd) continue;
            uint64_t got = perft(pos, d); total += got;
            if (got != exp) { fails++; printf("FAIL %s d%d exp %llu got %llu\n", fen.c_str(), d, (unsigned long long)exp, (unsigned long long)got); }
        }
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%d positions, %d failures, %.1fs, %.1f Mnps\n", n, fails, s, total / s / 1e6);
}
