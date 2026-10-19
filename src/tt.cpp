#include "tt.h"
#include <algorithm>
#include <cstring>
#ifdef __linux__
#include <sys/mman.h>
#endif

TT tt;

void TT::resize(size_t mb) {
    uint64_t n = (mb * 1024 * 1024) / sizeof(TTBucket);
    if (n < 1024) n = 1024;
    if (n == count && table) return;
    if (table) afree(table);
    table = nullptr;
    count = n;
}
void TT::ensure() {
    if (table) return;
#ifdef __linux__
    size_t bytes = (count * sizeof(TTBucket) + (2u << 20) - 1) & ~size_t((2u << 20) - 1);
    table = (TTBucket *)aligned_alloc(2u << 20, bytes);
    madvise(table, bytes, MADV_HUGEPAGE);
#else
    table = (TTBucket *)amalloc(count * sizeof(TTBucket));
#endif
    clear();
}
void TT::clear() {
    if (!table) return;
    memset(table, 0, count * sizeof(TTBucket));
    gen = 0;
}
TTEntry *TT::probe(U64 key, bool &hit) {
    TTBucket *b = bucket(key);
    uint16_t k16 = uint16_t(key);
    for (int i = 0; i < 3; i++)
        if (b->e[i].key == k16 && b->e[i].genBound) {
            hit = true;
            return &b->e[i];
        }
    hit = false;
    TTEntry *rep = &b->e[0];
    for (int i = 1; i < 3; i++) {
        int ra = rep->depth - ((uint8_t)(gen - (rep->genBound & 0xF8)) >> 2);
        int rb = b->e[i].depth - ((uint8_t)(gen - (b->e[i].genBound & 0xF8)) >> 2);
        if (rb < ra) rep = &b->e[i];
    }
    return rep;
}
void TT::store(TTEntry *e, U64 key, Move m, int score, int eval, int depth, int bound, bool pv) {
    uint16_t k16 = uint16_t(key);
    if (m || e->key != k16) e->move = m;
    if (bound == BOUND_EXACT || e->key != k16 || depth + 4 > e->depth || (e->genBound & 0xF8) != gen) {
        e->key = k16;
        e->score = int16_t(score);
        e->eval = int16_t(eval);
        e->depth = uint8_t(std::max(0, depth));
        e->genBound = uint8_t(gen | (pv ? 4 : 0) | bound);
    }
}
int TT::hashfull() const {
    int n = 0;
    for (int i = 0; i < 1000; i++)
        for (int j = 0; j < 3; j++)
            if (table[i].e[j].genBound && (table[i].e[j].genBound & 0xF8) == gen) n++;
    return n / 3;
}
