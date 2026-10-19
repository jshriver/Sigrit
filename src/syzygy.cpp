#include "syzygy.h"
#include <algorithm>
#include "fathom/tbprobe.h"

int tbProbeDepth = 1;
int tbProbeLimit = 7;
bool tb50MoveRule = true;
int tbCard = 0;
bool rootInTB = false;
int rootTBScore = 0;

TbInfo tb_set_path(const std::string &path) {
    TbInfo info;
    if (path.empty() || path == "<empty>") {
        tb_free();
        return info;
    }
    if (!tb_init(path.c_str())) return info;
    unsigned dtm;
    tb_file_counts(&info.wdl, &dtm, &info.dtz);
    info.pieces = TB_LARGEST;
    return info;
}

bool tb_probe(const Position &p, int &wdl) {
    unsigned r = tb_probe_wdl(p.byColor[WHITE], p.byColor[BLACK], p.byType[KING], p.byType[QUEEN], p.byType[ROOK],
                              p.byType[BISHOP], p.byType[KNIGHT], p.byType[PAWN], 0, 0,
                              p.s().ep == NO_SQ ? 0 : p.s().ep, p.stm == WHITE);
    if (r == TB_RESULT_FAILED) return false;
    wdl = int(r) - 2;
    return true;
}

static int tb_display_score(int tbScore) {
    if (tbScore >= TB_VALUE_MATE - TB_MAX_MATE_PLY - 1) return TB_WIN_SCORE;
    if (tbScore <= -TB_VALUE_MATE + TB_MAX_MATE_PLY + 1) return -TB_WIN_SCORE;
    return tbScore;
}

static unsigned tb_promo_code(int pt) {
    switch (pt) {
    case QUEEN: return TB_PROMOTES_QUEEN;
    case ROOK: return TB_PROMOTES_ROOK;
    case BISHOP: return TB_PROMOTES_BISHOP;
    case KNIGHT: return TB_PROMOTES_KNIGHT;
    }
    return TB_PROMOTES_NONE;
}

// Ranks root moves with DTZ (or WDL) tables and keeps only the best-ranked ones.
static TbRootMoves tbRoot;
void tb_rank_root(const Position &p, MoveList &legal) {
    rootInTB = false;
    tbCard = TB_LARGEST ? std::min<int>(TB_LARGEST, tbProbeLimit) : 0;
    if (!tbCard || popcount(p.occ()) > tbCard || p.s().castling) return;
    unsigned ep = p.s().ep == NO_SQ ? 0 : p.s().ep;
    bool dtz = tb_probe_root_dtz(p.byColor[WHITE], p.byColor[BLACK], p.byType[KING], p.byType[QUEEN],
                                 p.byType[ROOK], p.byType[BISHOP], p.byType[KNIGHT], p.byType[PAWN],
                                 p.s().halfmove, 0, ep, p.stm == WHITE, p.has_repeated(), tb50MoveRule, &tbRoot);
    if (!dtz && !tb_probe_root_wdl(p.byColor[WHITE], p.byColor[BLACK], p.byType[KING], p.byType[QUEEN],
                                   p.byType[ROOK], p.byType[BISHOP], p.byType[KNIGHT], p.byType[PAWN],
                                   p.s().halfmove, 0, ep, p.stm == WHITE, tb50MoveRule, &tbRoot))
        return;
    int rank[256], bestRank = -100000, bestScore = 0;
    for (int i = 0; i < legal.size; i++) {
        Move m = legal.moves[i];
        unsigned promo = move_type(m) == MT_PROMO ? tb_promo_code(move_promo(m)) : TB_PROMOTES_NONE;
        bool found = false;
        for (unsigned j = 0; j < tbRoot.size; j++) {
            TbMove t = tbRoot.moves[j].move;
            if (int(TB_MOVE_FROM(t)) == move_from(m) && int(TB_MOVE_TO(t)) == move_to(m) &&
                TB_MOVE_PROMOTES(t) == promo) {
                rank[i] = tbRoot.moves[j].tbRank;
                if (rank[i] > bestRank) bestRank = rank[i], bestScore = tbRoot.moves[j].tbScore;
                found = true;
                break;
            }
        }
        if (!found) return;  // move lists disagree: don't trust the probe
    }
    int n = 0;
    for (int i = 0; i < legal.size; i++)
        if (rank[i] == bestRank) legal.moves[n++] = legal.moves[i];
    legal.size = n;
    rootInTB = true;
    rootTBScore = tb_display_score(bestScore);
    // with DTZ the root filter already guarantees progress; probing in-tree only helps a WDL-only win
    if (dtz || rootTBScore <= 0) tbCard = 0;
}
