// Move-ordering statistics (per thread) and the search stack entry that points into them.
#pragma once
#include <cstring>
#include "types.h"

const int PH_SIZE = 1024;
const int CORR_SIZE = 16384;
const int CORR_GRAIN = 256;

extern int16_t dummyCH[12][64];  // continuation-history sentinel; never written

struct SStack {
    Move pv[MAX_PLY + 1];
    int pvLen;
    Move killer[2];
    int eval;
    Move move;
    int piece;
    Move excluded;
    int16_t (*ch)[64];
    int doubleExt;
    int cutoffCnt;
};

struct Histories {
    int16_t mainHist[2][4096];
    int16_t capHist[12][64][6];
    int16_t pawnHist[PH_SIZE][12][64];
    int16_t contHist[12][64][12][64];
    Move counterMove[12][64];
    int corrHist[2][CORR_SIZE];
    int npCorr[2][2][CORR_SIZE];
    int contCorr[2][12][64];

    void clear() {
        memset(mainHist, 0, sizeof(mainHist));
        memset(capHist, 0, sizeof(capHist));
        memset(pawnHist, 0, sizeof(pawnHist));
        memset(contHist, 0, sizeof(contHist));
        memset(counterMove, 0, sizeof(counterMove));
        memset(corrHist, 0, sizeof(corrHist));
        memset(npCorr, 0, sizeof(npCorr));
        memset(contCorr, 0, sizeof(contCorr));
    }
};
