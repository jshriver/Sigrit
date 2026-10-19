#include "position.h"
#include <algorithm>
#include <cctype>
#include <sstream>

U64 ZPiece[12][64], ZCastle[16], ZEp[8], ZSide;
bool Chess960 = false;

static U64 rng_state = 0x9E3779B97F4A7C15ULL;
static U64 rng() {
    U64 z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void init_zobrist() {
    for (int p = 0; p < 12; p++)
        for (int s = 0; s < 64; s++) ZPiece[p][s] = rng();
    ZCastle[0] = 0;
    U64 cr[4];
    for (int i = 0; i < 4; i++) cr[i] = rng();
    for (int c = 0; c < 16; c++) {
        ZCastle[c] = 0;
        for (int i = 0; i < 4; i++)
            if (c & (1 << i)) ZCastle[c] ^= cr[i];
    }
    for (int f = 0; f < 8; f++) ZEp[f] = rng();
    ZSide = rng();
}

// cuckoo tables for upcoming-repetition detection
U64 Cuckoo[8192];
static Move CuckooMove[8192];
static inline int cuckoo_h1(U64 k) { return int(k & 0x1FFF); }
static inline int cuckoo_h2(U64 k) { return int((k >> 16) & 0x1FFF); }

void init_cuckoo() {
    memset(Cuckoo, 0, sizeof(Cuckoo));
    memset(CuckooMove, 0, sizeof(CuckooMove));
    for (int p = 0; p < 12; p++) {
        int pt = piece_type(p);
        if (pt == PAWN) continue;
        for (int s1 = 0; s1 < 64; s1++)
            for (int s2 = s1 + 1; s2 < 64; s2++) {
                if (!(piece_attacks(pt, s1, 0) & sqbb(s2))) continue;
                Move mv = mk_move(s1, s2);
                U64 key = ZPiece[p][s1] ^ ZPiece[p][s2] ^ ZSide;
                int i = cuckoo_h1(key);
                while (true) {
                    std::swap(Cuckoo[i], key);
                    std::swap(CuckooMove[i], mv);
                    if (mv == MOVE_NONE) break;
                    i = (i == cuckoo_h1(key)) ? cuckoo_h2(key) : cuckoo_h1(key);
                }
            }
    }
}

bool Position::upcoming_repetition(int ply) const {
    const StateInfo &x = st[sp];
    int end = std::min(x.halfmove, x.pliesFromNull);
    if (end > sp) end = sp;
    if (end < 3) return false;
    U64 orig = x.key;
    U64 other = orig ^ st[sp - 1].key ^ ZSide;
    U64 o = occ();
    for (int i = 3; i <= end; i += 2) {
        other ^= st[sp - i + 1].key ^ st[sp - i].key ^ ZSide;
        if (other) continue;
        U64 moveKey = orig ^ st[sp - i].key;
        int j = cuckoo_h1(moveKey);
        if (Cuckoo[j] != moveKey) {
            j = cuckoo_h2(moveKey);
            if (Cuckoo[j] != moveKey) continue;
        }
        Move mv = CuckooMove[j];
        int s1 = move_from(mv), s2 = move_to(mv);
        if (!(Between[s1][s2] & o)) {
            if (ply > i) return true;
        }
    }
    return false;
}

bool Position::has_repeated() const {
    int end = std::min(st[sp].halfmove, st[sp].pliesFromNull);
    if (end > sp) end = sp;
    for (int i = 0; i <= end; i++)
        for (int j = i + 4; j <= end; j += 2)
            if (st[sp - j].key == st[sp - i].key) return true;
    return false;
}

void Position::copy_from(const Position &o) {
    memcpy(byType, o.byType, sizeof(byType));
    memcpy(byColor, o.byColor, sizeof(byColor));
    memcpy(board, o.board, sizeof(board));
    stm = o.stm;
    gamePly = o.gamePly;
    memcpy(castleRookSq, o.castleRookSq, sizeof(castleRookSq));
    memcpy(castlePath, o.castlePath, sizeof(castlePath));
    memcpy(castleKingPath, o.castleKingPath, sizeof(castleKingPath));
    memcpy(castleMask, o.castleMask, sizeof(castleMask));
    int keep = std::min(o.sp, std::min(o.st[o.sp].halfmove, 200));
    for (int i = 0; i <= keep; i++) st[i] = o.st[o.sp - keep + i];
    sp = keep;
    dirtyOut = nullptr;
}

void Position::clear() {
    memset(byType, 0, sizeof(byType));
    memset(byColor, 0, sizeof(byColor));
    for (int i = 0; i < 64; i++) board[i] = NO_PIECE;
    stm = WHITE;
    sp = 0;
    gamePly = 0;
    memset(&st[0], 0, sizeof(StateInfo));
    st[0].ep = NO_SQ;
    for (int r = 0; r < 4; r++) castleRookSq[r] = NO_SQ, castlePath[r] = castleKingPath[r] = 0;
    memset(castleMask, 15, sizeof(castleMask));
}

void Position::add_castling(int c, int rsq) {
    int ksq = king_sq(c);
    int back = c == WHITE ? 0 : 7;
    if (rank_of(ksq) != back || rank_of(rsq) != back || board[rsq] != make_piece(c, ROOK)) return;
    int r = 2 * c + (rsq < ksq);
    int kto = castle_king_to(r), rto = castle_rook_to(r);
    castleRookSq[r] = rsq;
    castlePath[r] = (Between[rsq][rto] | Between[ksq][kto] | sqbb(rto) | sqbb(kto)) & ~(sqbb(ksq) | sqbb(rsq));
    castleKingPath[r] = Between[ksq][kto] | sqbb(kto);
    castleMask[ksq] &= ~(c == WHITE ? 3 : 12);
    castleMask[rsq] &= ~(1 << r);
    st[0].castling |= 1 << r;
}

U64 Position::compute_key() const {
    U64 k = 0;
    for (int s = 0; s < 64; s++)
        if (board[s] != NO_PIECE) k ^= ZPiece[board[s]][s];
    k ^= ZCastle[st[sp].castling];
    if (st[sp].ep != NO_SQ) k ^= ZEp[file_of(st[sp].ep)];
    if (stm == BLACK) k ^= ZSide;
    return k;
}

U64 Position::compute_pawn_key() const {
    U64 k = 0;
    U64 b = byType[PAWN];
    while (b) {
        int s = pop_lsb(b);
        k ^= ZPiece[board[s]][s];
    }
    return k;
}

U64 Position::compute_np_key(int c) const {
    U64 k = 0;
    U64 b = byColor[c] & ~byType[PAWN];
    while (b) {
        int s = pop_lsb(b);
        k ^= ZPiece[board[s]][s];
    }
    return k;
}

void Position::compute_check_info() {
    StateInfo &x = st[sp];
    int ksq = king_sq(stm), them = stm ^ 1;
    x.checkers = attackers_to(ksq, occ()) & byColor[them];
    x.pinned = 0;
    U64 snipers = (rook_attacks(ksq, 0) & (pieces(them, ROOK) | pieces(them, QUEEN))) |
                  (bishop_attacks(ksq, 0) & (pieces(them, BISHOP) | pieces(them, QUEEN)));
    U64 o = occ();
    while (snipers) {
        int sn = pop_lsb(snipers);
        U64 b = Between[ksq][sn] & o;
        if (b && !(b & (b - 1))) x.pinned |= b & byColor[stm];
    }
}

bool Position::set_fen(const std::string &fen) {
    clear();
    std::istringstream is(fen);
    std::string bd, side, cast, ep;
    int hm = 0, fm = 1;
    is >> bd >> side >> cast >> ep;
    if (!(is >> hm)) hm = 0;
    if (!(is >> fm)) fm = 1;
    int r = 7, f = 0;
    for (char c : bd) {
        if (c == '/') {
            r--;
            f = 0;
        } else if (c >= '1' && c <= '8') {
            f += c - '0';
        } else {
            const char *pcs = "PNBRQKpnbrqk";
            const char *q = strchr(pcs, c);
            if (!q || r < 0 || f > 7) return false;
            int idx = int(q - pcs);
            put(make_piece(idx / 6, idx % 6), r * 8 + f);
            f++;
        }
    }
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return false;
    stm = (side == "b") ? BLACK : WHITE;
    if (sq_attacked(king_sq(stm ^ 1), stm, occ())) return false;  // side not to move is in check
    // castling: KQkq (outermost rook, X-FEN) or Shredder-FEN rook files (AHah)
    st[0].castling = 0;
    for (char ch : cast) {
        if (ch == '-') break;
        int c = isupper((unsigned char)ch) ? WHITE : BLACK;
        char u = char(toupper((unsigned char)ch));
        int back = c == WHITE ? 0 : 56, ksq = king_sq(c), rsq = NO_SQ;
        U64 rooks = pieces(c, ROOK) & (RANK_1 << back);
        if (u == 'K') {
            for (int s = back + 7; s > ksq && s >= back; s--)
                if (rooks & sqbb(s)) { rsq = s; break; }
        } else if (u == 'Q') {
            for (int s = back; s < ksq && s <= back + 7; s++)
                if (rooks & sqbb(s)) { rsq = s; break; }
        } else if (u >= 'A' && u <= 'H')
            rsq = back + (u - 'A');
        if (rsq != NO_SQ) add_castling(c, rsq);
    }
    st[0].ep = NO_SQ;
    if (ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' && ep[1] >= '1' && ep[1] <= '8') {
        int s = (ep[1] - '1') * 8 + (ep[0] - 'a');
        if (PawnAtt[stm ^ 1][s] & pieces(stm, PAWN)) st[0].ep = s;
    }
    st[0].halfmove = hm;
    st[0].captured = NO_PIECE;
    st[0].pliesFromNull = 0;
    gamePly = 2 * (fm - 1) + (stm == BLACK);
    st[0].key = compute_key();
    st[0].pawnKey = compute_pawn_key();
    st[0].npKey[0] = compute_np_key(WHITE);
    st[0].npKey[1] = compute_np_key(BLACK);
    compute_check_info();
    return true;
}

std::string Position::fen() const {
    std::string r;
    for (int rk = 7; rk >= 0; rk--) {
        int e = 0;
        for (int f = 0; f < 8; f++) {
            int p = board[rk * 8 + f];
            if (p == NO_PIECE) {
                e++;
                continue;
            }
            if (e) r += char('0' + e), e = 0;
            r += "PNBRQKpnbrqk"[p];
        }
        if (e) r += char('0' + e);
        if (rk) r += '/';
    }
    r += stm == WHITE ? " w " : " b ";
    int c = st[sp].castling;
    if (!c) r += '-';
    const char *std = "KQkq";
    for (int i = 0; i < 4; i++)
        if (c & (1 << i)) {
            char f = char('a' + file_of(castleRookSq[i]));
            r += Chess960 ? (i < 2 ? char(toupper(f)) : f) : std[i];
        }
    r += ' ';
    if (st[sp].ep == NO_SQ) r += '-';
    else {
        r += char('a' + file_of(st[sp].ep));
        r += char('1' + rank_of(st[sp].ep));
    }
    r += " " + std::to_string(st[sp].halfmove) + " " + std::to_string(gamePly / 2 + 1);
    return r;
}

void Position::make_move(Move m) {
    StateInfo &prev = st[sp];
    StateInfo &x = st[sp + 1];
    sp++;
    gamePly++;
    x.castling = prev.castling;
    x.halfmove = prev.halfmove + 1;
    x.pliesFromNull = prev.pliesFromNull + 1;
    U64 key = prev.key ^ ZSide;
    U64 pkey = prev.pawnKey;
    x.npKey[0] = prev.npKey[0];
    x.npKey[1] = prev.npKey[1];
    if (prev.ep != NO_SQ) key ^= ZEp[file_of(prev.ep)];
    x.ep = NO_SQ;

    int from = move_from(m), to = move_to(m), mt = move_type(m);
    int us = stm, them = us ^ 1;
    int p = board[from];
    int pt = piece_type(p);
    DirtyPiece *dp = dirtyOut ? &dirtyOut[sp] : nullptr;
    if (dp) dp->n = 0;

    int captured = NO_PIECE;
    if (mt == MT_CASTLE) {
        int cr = castle_right(m);
        int rf = to, rt = castle_rook_to(cr), kt = castle_king_to(cr);
        int rook = board[rf];
        remove(from);
        remove(rf);
        put(p, kt);
        put(rook, rt);
        key ^= ZPiece[p][from] ^ ZPiece[p][kt] ^ ZPiece[rook][rf] ^ ZPiece[rook][rt];
        x.npKey[us] ^= ZPiece[p][from] ^ ZPiece[p][kt] ^ ZPiece[rook][rf] ^ ZPiece[rook][rt];
        if (dp) {
            dp->n = 2;
            dp->piece[0] = p, dp->from[0] = from, dp->to[0] = kt;
            dp->piece[1] = rook, dp->from[1] = rf, dp->to[1] = rt;
        }
    } else {
        int capsq = to;
        if (mt == MT_EP) capsq = to - (us == WHITE ? 8 : -8);
        captured = board[capsq];
        if (captured != NO_PIECE) {
            remove(capsq);
            key ^= ZPiece[captured][capsq];
            if (piece_type(captured) == PAWN) pkey ^= ZPiece[captured][capsq];
            else x.npKey[them] ^= ZPiece[captured][capsq];
            x.halfmove = 0;
        }
        move_piece(from, to);
        key ^= ZPiece[p][from] ^ ZPiece[p][to];
        if (pt != PAWN) x.npKey[us] ^= ZPiece[p][from] ^ ZPiece[p][to];
        if (dp) {
            dp->n = 1;
            dp->piece[0] = p, dp->from[0] = from, dp->to[0] = to;
            if (captured != NO_PIECE) {
                dp->n = 2;
                dp->piece[1] = captured, dp->from[1] = capsq, dp->to[1] = NO_SQ;
            }
        }
        if (pt == PAWN) {
            x.halfmove = 0;
            pkey ^= ZPiece[p][from];
            if (mt == MT_PROMO) {
                int np = make_piece(us, move_promo(m));
                remove(to);
                put(np, to);
                key ^= ZPiece[p][to] ^ ZPiece[np][to];
                x.npKey[us] ^= ZPiece[np][to];
                if (dp) {
                    dp->to[0] = NO_SQ;
                    int k = dp->n++;
                    dp->piece[k] = np, dp->from[k] = NO_SQ, dp->to[k] = to;
                }
            } else {
                pkey ^= ZPiece[p][to];
                if ((to ^ from) == 16) {
                    int epsq = (to + from) / 2;
                    if (PawnAtt[us][epsq] & pieces(them, PAWN)) {
                        x.ep = epsq;
                        key ^= ZEp[file_of(epsq)];
                    }
                }
            }
        }
    }
    x.captured = captured;
    if (x.castling) {
        int nc = x.castling & castleMask[from] & castleMask[to];
        if (nc != x.castling) {
            key ^= ZCastle[x.castling] ^ ZCastle[nc];
            x.castling = nc;
        }
    }
    x.key = key;
    x.pawnKey = pkey;
    stm = them;
    compute_check_info();
}

void Position::unmake_move(Move m) {
    int from = move_from(m), to = move_to(m), mt = move_type(m);
    stm ^= 1;
    int us = stm;
    StateInfo &x = st[sp];
    if (mt == MT_CASTLE) {
        int cr = castle_right(m);
        int rt = castle_rook_to(cr), kt = castle_king_to(cr);
        int king = board[kt], rook = board[rt];
        remove(kt);
        remove(rt);
        put(king, from);
        put(rook, to);
    } else {
        if (mt == MT_PROMO) {
            remove(to);
            put(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (x.captured != NO_PIECE) {
            int capsq = to;
            if (mt == MT_EP) capsq = to - (us == WHITE ? 8 : -8);
            put(x.captured, capsq);
        }
    }
    sp--;
    gamePly--;
}

void Position::make_null() {
    StateInfo &prev = st[sp];
    StateInfo &x = st[sp + 1];
    sp++;
    gamePly++;
    x = prev;
    x.key ^= ZSide;
    if (prev.ep != NO_SQ) x.key ^= ZEp[file_of(prev.ep)];
    x.ep = NO_SQ;
    x.halfmove = prev.halfmove + 1;
    x.pliesFromNull = 0;
    x.captured = NO_PIECE;
    if (dirtyOut) dirtyOut[sp].n = 0;
    stm ^= 1;
    compute_check_info();
}

void Position::unmake_null() {
    stm ^= 1;
    sp--;
    gamePly--;
}

static const int SeeVal[7] = {100, 300, 300, 500, 900, 20000, 0};

bool Position::see_ge(Move m, int threshold) const {
    if (move_type(m) != MT_NORMAL) return 0 >= threshold;
    int from = move_from(m), to = move_to(m);
    int swap = SeeVal[board[to] == NO_PIECE ? 6 : piece_type(board[to])] - threshold;
    if (swap < 0) return false;
    swap = SeeVal[piece_type(board[from])] - swap;
    if (swap <= 0) return true;
    U64 o = occ() ^ sqbb(from) ^ sqbb(to);
    int side = stm;
    U64 attackers = attackers_to(to, o);
    U64 bq = byType[BISHOP] | byType[QUEEN], rq = byType[ROOK] | byType[QUEEN];
    int res = 1;
    while (true) {
        side ^= 1;
        attackers &= o;
        U64 sa = attackers & byColor[side];
        if (!sa) break;
        res ^= 1;
        U64 b;
        if ((b = sa & byType[PAWN])) {
            if ((swap = SeeVal[PAWN] - swap) < res) break;
            o ^= b & -b;
            attackers |= bishop_attacks(to, o) & bq;
        } else if ((b = sa & byType[KNIGHT])) {
            if ((swap = SeeVal[KNIGHT] - swap) < res) break;
            o ^= b & -b;
        } else if ((b = sa & byType[BISHOP])) {
            if ((swap = SeeVal[BISHOP] - swap) < res) break;
            o ^= b & -b;
            attackers |= bishop_attacks(to, o) & bq;
        } else if ((b = sa & byType[ROOK])) {
            if ((swap = SeeVal[ROOK] - swap) < res) break;
            o ^= b & -b;
            attackers |= rook_attacks(to, o) & rq;
        } else if ((b = sa & byType[QUEEN])) {
            if ((swap = SeeVal[QUEEN] - swap) < res) break;
            o ^= b & -b;
            attackers |= (bishop_attacks(to, o) & bq) | (rook_attacks(to, o) & rq);
        } else {
            return (attackers & ~byColor[side]) ? res ^ 1 : res;
        }
    }
    return res;
}

bool Position::is_draw(int ply) const {
    const StateInfo &x = st[sp];
    if (x.halfmove >= 100) {
        if (!x.checkers) return true;
        MoveList ml;
        generate(GEN_ALL, ml);
        for (int i = 0; i < ml.size; i++)
            if (is_legal(ml.moves[i])) return true;
        return false;
    }
    int end = x.halfmove < x.pliesFromNull ? x.halfmove : x.pliesFromNull;
    if (end > sp) end = sp;
    int reps = 0;
    for (int i = 4; i <= end; i += 2) {
        if (st[sp - i].key == x.key) {
            // repetition inside the search tree → draw; before root need 3-fold
            if (i < ply) return true;
            if (++reps >= 2) return true;
        }
    }
    return false;
}

