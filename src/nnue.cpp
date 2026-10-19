#include "nnue.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>

// SIMD: AVX2 on x86 (AVX-512 builds use it too). Everything else, e.g. ARMv8, uses plain C++
// that the compiler auto-vectorizes (NEON). Both paths give bit-identical evaluations.
#if defined(__AVX2__) && !defined(NNUE_GENERIC)
#define NNUE_AVX2 1
#include <immintrin.h>
#endif

#ifndef NET_PATH
#define NET_PATH "../nets/net.nnue"
#endif

#if defined(_WIN32)
#define NET_SECTION ".section .rdata,\"dr\"\n"
#define NET_SECTION_END ".text\n"
#elif defined(__APPLE__)
#define NET_SECTION ".section __TEXT,__const\n"
#define NET_SECTION_END ".text\n"
#else
#define NET_SECTION ".pushsection .rodata\n"
#define NET_SECTION_END ".popsection\n"
#endif

#if defined(__APPLE__)
#define NET_SYM(x) "_" x
#else
#define NET_SYM(x) x
#endif

asm(NET_SECTION
    ".balign 64\n"
    ".global " NET_SYM("gEmbeddedNet") "\n"
    NET_SYM("gEmbeddedNet") ":\n"
    ".incbin \"" NET_PATH "\"\n"
    ".global " NET_SYM("gEmbeddedNetEnd") "\n"
    NET_SYM("gEmbeddedNetEnd") ":\n"
    ".byte 0\n"
    NET_SECTION_END);
extern "C" const unsigned char gEmbeddedNet[];
extern "C" const unsigned char gEmbeddedNetEnd[];

namespace nnue {

static const int KingBuckets[64] = {
    0, 1, 2, 3, 3, 2, 1, 0,
    4, 4, 5, 5, 5, 5, 4, 4,
    6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
};

alignas(64) static int16_t L0W[8 * 768 * HL];
alignas(64) static int16_t L0B[HL];
alignas(64) static int16_t L1W[8][L2][2 * HL];
alignas(64) static int16_t L1T[8][HL][L2][2];  // [ob][input pair][output][2]
#ifndef NNUE_AVX2
alignas(64) static int16_t L1G[8][HL][2][L2];  // [ob][input pair][half][output]: contiguous per half
#endif
alignas(64) static float L1B[8][L2];
alignas(64) static float L2W[8][L3][L2];
alignas(64) static float L2B[8][L3];
alignas(64) static float L2T[8][L2][L3];  // [ob][in][out]
alignas(16) static uint16_t NzLut[256][8];
alignas(64) static float L3W[8][L3];
static float L3B[8];

bool init() {
    size_t size = size_t(gEmbeddedNetEnd - gEmbeddedNet);
    if (size < 13128224) return false;
    const unsigned char *p = gEmbeddedNet;
    auto take = [&](void *dst, size_t n) {
        memcpy(dst, p, n);
        p += n;
    };
    take(L0W, sizeof(L0W));
    take(L0B, sizeof(L0B));
    take(L1W, sizeof(L1W));
    take(L1B, sizeof(L1B));
    take(L2W, sizeof(L2W));
    take(L2B, sizeof(L2B));
    take(L3W, sizeof(L3W));
    take(L3B, sizeof(L3B));
    int mx = 0;
    for (int b = 0; b < 8; b++)
        for (int o = 0; o < L2; o++)
            for (int i = 0; i < 2 * HL; i++) mx = std::max(mx, std::abs((int)L1W[b][o][i]));
    for (int m = 0; m < 256; m++) {
        int n = 0;
        for (int b = 0; b < 8; b++)
            if (m & (1 << b)) NzLut[m][n++] = uint16_t(b);
    }
    for (int b = 0; b < 8; b++)
        for (int o = 0; o < L2; o++)
            for (int j = 0; j < L3; j++) L2T[b][o][j] = L2W[b][j][o];
    for (int b = 0; b < 8; b++)
        for (int k = 0; k < HL; k++)
            for (int o = 0; o < L2; o++) {
                L1T[b][k][o][0] = L1W[b][o][2 * k];
                L1T[b][k][o][1] = L1W[b][o][2 * k + 1];
#ifndef NNUE_AVX2
                L1G[b][k][0][o] = L1W[b][o][2 * k];
                L1G[b][k][1][o] = L1W[b][o][2 * k + 1];
#endif
            }
    if (mx > 127) {
        fprintf(stderr, "warning: l1w max %d exceeds 127\n", mx);
    }
    return true;
}

static inline int king_bucket(int persp, int ksq) { return KingBuckets[ksq ^ (persp ? 56 : 0)]; }

static inline int feature_index(int persp, int bucket, int piece, int sq) {
    int flip = persp ? 56 : 0;
    return bucket * 768 + 384 * (piece_color(piece) != persp) + 64 * piece_type(piece) + (sq ^ flip);
}

static inline const int16_t *wrow(int idx) { return L0W + size_t(idx) * HL; }

Evaluator::Evaluator() {
    acc = (Accumulator *)amalloc(sizeof(Accumulator) * MAX_STATES);
    dirty = (DirtyPiece *)malloc(sizeof(DirtyPiece) * MAX_STATES);
    memset(dirty, 0, sizeof(DirtyPiece) * MAX_STATES);
    for (int i = 0; i < MAX_STATES; i++) acc[i].computed[0] = acc[i].computed[1] = false;
}
Evaluator::~Evaluator() {
    afree(acc);
    free(dirty);
}

void Evaluator::attach(Position &pos) { pos.dirtyOut = dirty; }

void Evaluator::reset(const Position &pos) {
    for (int c = 0; c < 2; c++)
        for (int b = 0; b < 8; b++) {
            memcpy(cache[c][b].v, L0B, sizeof(L0B));
            memset(cache[c][b].bb, 0, sizeof(cache[c][b].bb));
        }
    for (int i = 0; i < MAX_STATES; i++) acc[i].computed[0] = acc[i].computed[1] = false;
    for (int c = 0; c < 2; c++) {
        refresh(pos, c, acc[pos.sp].v[c]);
        acc[pos.sp].computed[c] = true;
    }
}

// dst = src + sum(adds) - sum(subs) (int16, wrapping); also written to dst2 if given.
static void apply_rows(const int16_t *src, int16_t *dst, int16_t *dst2, const int16_t *const *adds, int na,
                       const int16_t *const *subs, int ns) {
#ifdef NNUE_AVX2
    const __m256i *s = (const __m256i *)src;
    __m256i *d = (__m256i *)dst, *d2 = (__m256i *)dst2;
    for (int t = 0; t < HL / 16; t += 16) {
        __m256i r[16];
        for (int q = 0; q < 16; q++) r[q] = _mm256_load_si256(s + t + q);
        for (int i = 0; i < na; i++) {
            const __m256i *w = (const __m256i *)adds[i] + t;
            for (int q = 0; q < 16; q++) r[q] = _mm256_add_epi16(r[q], _mm256_load_si256(w + q));
        }
        for (int i = 0; i < ns; i++) {
            const __m256i *w = (const __m256i *)subs[i] + t;
            for (int q = 0; q < 16; q++) r[q] = _mm256_sub_epi16(r[q], _mm256_load_si256(w + q));
        }
        for (int q = 0; q < 16; q++) _mm256_store_si256(d + t + q, r[q]);
        if (d2)
            for (int q = 0; q < 16; q++) _mm256_store_si256(d2 + t + q, r[q]);
    }
#else
    const int TILE = 128;  // 16 NEON registers
    for (int t = 0; t < HL; t += TILE) {
        int16_t r[TILE];
        for (int j = 0; j < TILE; j++) r[j] = src[t + j];
        for (int i = 0; i < na; i++)
            for (int j = 0; j < TILE; j++) r[j] = int16_t(r[j] + adds[i][t + j]);
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < TILE; j++) r[j] = int16_t(r[j] - subs[i][t + j]);
        for (int j = 0; j < TILE; j++) dst[t + j] = r[j];
        if (dst2)
            for (int j = 0; j < TILE; j++) dst2[t + j] = r[j];
    }
#endif
}

void Evaluator::refresh(const Position &pos, int persp, int16_t *out) {
    int bucket = king_bucket(persp, pos.king_sq(persp));
    CacheEntry &e = cache[persp][bucket];
    const int16_t *adds[32], *subs[32];
    int na = 0, ns = 0;
    for (int p = 0; p < 12; p++) {
        U64 cur = pos.byColor[piece_color(p)] & pos.byType[piece_type(p)];
        U64 add = cur & ~e.bb[p], rem = e.bb[p] & ~cur;
        while (add) adds[na++] = wrow(feature_index(persp, bucket, p, pop_lsb(add)));
        while (rem) subs[ns++] = wrow(feature_index(persp, bucket, p, pop_lsb(rem)));
        e.bb[p] = cur;
    }
    apply_rows(e.v, e.v, out, adds, na, subs, ns);
}

void Evaluator::update(const Position &pos, int persp) {
    int sp = pos.sp;
    int ksq = pos.king_sq(persp);
    int bucket = king_bucket(persp, ksq);
    // walk back to a computed accumulator, abort on own king bucket change
    int j = sp;
    while (!acc[j].computed[persp]) {
        const DirtyPiece &d = dirty[j];
        if (d.n && d.piece[0] == make_piece(persp, KING) &&
            king_bucket(persp, d.from[0]) != king_bucket(persp, d.to[0])) {
            refresh(pos, persp, acc[sp].v[persp]);
            acc[sp].computed[persp] = true;
            return;
        }
        if (j == 0) {
            refresh(pos, persp, acc[sp].v[persp]);
            acc[sp].computed[persp] = true;
            return;
        }
        j--;
    }
    for (int k = j + 1; k <= sp; k++) {
        const DirtyPiece &d = dirty[k];
        const int16_t *add[2], *sub[2];
        int na = 0, ns = 0;
        for (int i = 0; i < d.n; i++) {
            if (d.from[i] != NO_SQ) sub[ns++] = wrow(feature_index(persp, bucket, d.piece[i], d.from[i]));
            if (d.to[i] != NO_SQ) add[na++] = wrow(feature_index(persp, bucket, d.piece[i], d.to[i]));
        }
        apply_rows(acc[k - 1].v[persp], acc[k].v[persp], nullptr, add, na, sub, ns);
        acc[k].computed[persp] = true;
    }
}

static int forward(const int16_t *us, const int16_t *them, int ob) {
    // sq = clamp(x,0,QA)^2 >> 1 (fits i16); sparse over nonzero input pairs
    alignas(64) int16_t sq[2 * HL];
    alignas(64) uint16_t nz[HL + 8];
    int nnz = 0;
#ifdef NNUE_AVX2
    const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
    for (int half = 0; half < 2; half++) {
        const int16_t *src = half ? them : us;
        for (int i = 0; i < HL; i += 16) {
            __m256i a = _mm256_load_si256((const __m256i *)(src + i));
            a = _mm256_min_epi16(_mm256_max_epi16(a, zero), qa);
            a = _mm256_srli_epi16(_mm256_mullo_epi16(a, a), 1);
            _mm256_store_si256((__m256i *)(sq + half * HL + i), a);
            unsigned mask = _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(a, zero)));
            __m128i base = _mm_set1_epi16(int16_t((half * HL + i) / 2));
            _mm_storeu_si128((__m128i *)(nz + nnz), _mm_add_epi16(base, _mm_load_si128((const __m128i *)NzLut[mask])));
            nnz += __builtin_popcount(mask);
        }
    }
#else
    for (int half = 0; half < 2; half++) {
        const int16_t *src = half ? them : us;
        int16_t *dst = sq + half * HL;
        for (int i = 0; i < HL; i++) {
            int a = std::min<int>(std::max<int>(src[i], 0), QA);
            dst[i] = int16_t(unsigned(a * a) >> 1);
        }
    }
    for (int j = 0; j < HL; j++) {  // input pair j = (sq[2j], sq[2j+1]), in ascending order
        nz[nnz] = uint16_t(j);
        nnz += (sq[2 * j] | sq[2 * j + 1]) != 0;
    }
#endif
    int64_t tot[L2] = {0};
#ifdef NNUE_AVX2
    const int16_t *wb = &L1T[ob][0][0][0];
    const int32_t *sq32 = (const int32_t *)sq;
    __m256i a0 = zero, a1 = zero, a2 = zero, a3 = zero;
    nz[nnz] = nz[nnz > 0 ? nnz - 1 : 0];  // pad (duplicate handled below)
    int k = 0;
    auto flush = [&]() {
        alignas(32) int32_t l[16];
        _mm256_store_si256((__m256i *)l, _mm256_add_epi32(a0, a2));
        _mm256_store_si256((__m256i *)(l + 8), _mm256_add_epi32(a1, a3));
        for (int o = 0; o < L2; o++) tot[o] += l[o];
        a0 = a1 = a2 = a3 = zero;
    };
    while (k + 1 < nnz) {
        int end = std::min(nnz - 1, k + 256);
        for (; k < end; k += 2) {
            int p = nz[k], q = nz[k + 1];
            __m256i b = _mm256_set1_epi32(sq32[p]);
            __m256i c = _mm256_set1_epi32(sq32[q]);
            const __m256i *w = (const __m256i *)(wb + p * 32);
            const __m256i *v = (const __m256i *)(wb + q * 32);
            a0 = _mm256_add_epi32(a0, _mm256_madd_epi16(b, _mm256_load_si256(w)));
            a1 = _mm256_add_epi32(a1, _mm256_madd_epi16(b, _mm256_load_si256(w + 1)));
            a2 = _mm256_add_epi32(a2, _mm256_madd_epi16(c, _mm256_load_si256(v)));
            a3 = _mm256_add_epi32(a3, _mm256_madd_epi16(c, _mm256_load_si256(v + 1)));
        }
        flush();
    }
    if (k < nnz) {
        int p = nz[k];
        __m256i b = _mm256_set1_epi32(sq32[p]);
        const __m256i *w = (const __m256i *)(wb + p * 32);
        a0 = _mm256_madd_epi16(b, _mm256_load_si256(w));
        a1 = _mm256_madd_epi16(b, _mm256_load_si256(w + 1));
        flush();
    }
#else
    // same pairing and flush points as the AVX2 code; int32 sums wrap identically (uint32)
    uint32_t a[L2] = {0}, c[L2] = {0};
    const int16_t (*wg)[2][L2] = L1G[ob];
    auto madd = [&](uint32_t *acc, int p) {
        int x0 = sq[2 * p], x1 = sq[2 * p + 1];
        const int16_t *w0 = wg[p][0], *w1 = wg[p][1];
        for (int o = 0; o < L2; o++) acc[o] += uint32_t(x0 * w0[o] + x1 * w1[o]);
    };
    auto flush = [&]() {
        for (int o = 0; o < L2; o++) {
            tot[o] += int32_t(a[o] + c[o]);
            a[o] = c[o] = 0;
        }
    };
    int k = 0;
    while (k + 1 < nnz) {
        int end = std::min(nnz - 1, k + 256);
        for (; k < end; k += 2) madd(a, nz[k]), madd(c, nz[k + 1]);
        flush();
    }
    if (k < nnz) {
        madd(a, nz[k]);
        flush();
    }
#endif
    float h2[L2];
    for (int o = 0; o < L2; o++) {
        float x = float(double(tot[o]) * (2.0 / double(QA * QA * QB))) + L1B[ob][o];
        x = x < 0.f ? 0.f : x > 1.f ? 1.f : x;
        h2[o] = x * x;
    }
#ifdef NNUE_AVX2
    __m256 h3v[4];
    for (int q = 0; q < 4; q++) h3v[q] = _mm256_load_ps(&L2B[ob][q * 8]);
    for (int o = 0; o < L2; o++) {
        __m256 x = _mm256_set1_ps(h2[o]);
        for (int q = 0; q < 4; q++) h3v[q] = _mm256_fmadd_ps(x, _mm256_load_ps(&L2T[ob][o][q * 8]), h3v[q]);
    }
    const __m256 zf = _mm256_setzero_ps(), onef = _mm256_set1_ps(1.0f);
    __m256 sum = zf;
    for (int q = 0; q < 4; q++) {
        __m256 x = _mm256_min_ps(_mm256_max_ps(h3v[q], zf), onef);
        sum = _mm256_fmadd_ps(_mm256_mul_ps(x, x), _mm256_load_ps(&L3W[ob][q * 8]), sum);
    }
    __m128 s4 = _mm_add_ps(_mm256_castps256_ps128(sum), _mm256_extractf128_ps(sum, 1));
    s4 = _mm_add_ps(s4, _mm_movehl_ps(s4, s4));
    s4 = _mm_add_ss(s4, _mm_shuffle_ps(s4, s4, 1));
    float out = L3B[ob] + _mm_cvtss_f32(s4);
#else
    // lane-for-lane the AVX2 computation: same fused multiply-adds, same reduction order
    float h3[L3];
    for (int j = 0; j < L3; j++) h3[j] = L2B[ob][j];
    for (int o = 0; o < L2; o++)
        for (int j = 0; j < L3; j++) h3[j] = std::fma(h2[o], L2T[ob][o][j], h3[j]);
    float lane[8] = {0};
    for (int q = 0; q < 4; q++)
        for (int j = 0; j < 8; j++) {
            float x = std::min(std::max(h3[q * 8 + j], 0.0f), 1.0f);
            float x2 = x * x;
            lane[j] = std::fma(x2, L3W[ob][q * 8 + j], lane[j]);
        }
    float t0 = lane[0] + lane[4], t1 = lane[1] + lane[5], t2 = lane[2] + lane[6], t3 = lane[3] + lane[7];
    float out = L3B[ob] + ((t0 + t2) + (t1 + t3));
#endif
    return int(out * 400.0f);
}

int Evaluator::evaluate(const Position &pos) {
    if (!acc[pos.sp].computed[0]) update(pos, 0);
    if (!acc[pos.sp].computed[1]) update(pos, 1);
    int ob = (popcount(pos.occ()) - 2) / 4;
    const Accumulator &a = acc[pos.sp];
    return forward(a.v[pos.stm], a.v[pos.stm ^ 1], ob);
}

int evaluate_full(const Position &pos) {
    alignas(64) int16_t a[2][HL];
    for (int c = 0; c < 2; c++) {
        int bucket = king_bucket(c, pos.king_sq(c));
        memcpy(a[c], L0B, sizeof(L0B));
        for (int s = 0; s < 64; s++) {
            if (pos.board[s] == NO_PIECE) continue;
            const int16_t *w = wrow(feature_index(c, bucket, pos.board[s], s));
            for (int i = 0; i < HL; i++) a[c][i] += w[i];
        }
    }
    int ob = (popcount(pos.occ()) - 2) / 4;
    return forward(a[pos.stm], a[pos.stm ^ 1], ob);
}

}  // namespace nnue
