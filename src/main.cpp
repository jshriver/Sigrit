#include <cstdio>
#include "bitboard.h"
#include "nnue.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

int main(int argc, char **argv) {
    init_attacks();
    init_zobrist();
    init_cuckoo();
    if (!nnue::init()) {
        fprintf(stderr, "failed to load embedded network\n");
        return 1;
    }
    search_init();
    tt.resize(256);
    set_threads(1);
    uci_loop(argc, argv);
    return 0;
}
