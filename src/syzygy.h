// Syzygy tablebases via Fathom: root move ranking and in-search WDL probes.
// Fathom's own header stays inside syzygy.cpp (its macros, e.g. TB_WIN, clash with engine names).
#pragma once
#include <string>
#include "position.h"

// UCI options
extern int tbProbeDepth;
extern int tbProbeLimit;
extern bool tb50MoveRule;

// Per-search state, set by tb_rank_root
extern int tbCard;        // in-search probing limit (0 = off)
extern bool rootInTB;     // root moves were filtered by the tablebases
extern int rootTBScore;   // score to report while rootInTB

struct TbInfo {
    unsigned wdl = 0, dtz = 0;  // .rtbw / .rtbz files found
    unsigned pieces = 0;        // largest piece count covered (0 = none)
};
// (Re)loads tablebases from a path list (':' separated, ';' on Windows). "<empty>" or "" unloads.
TbInfo tb_set_path(const std::string &path);
// WDL probe (-2 loss .. +2 win, +-1 cursed/blessed). Needs rule50 == 0 and no castling rights.
bool tb_probe(const Position &p, int &wdl);
// Ranks root moves with DTZ (or WDL) tables and keeps only the best-ranked ones.
void tb_rank_root(const Position &p, MoveList &legal);
