// Position: board state, FEN, make/unmake, SEE, draw and repetition detection.
#pragma once
#include <cstring>
#include <string>
#include "bitboard.h"
#include "movegen.h"

// ---------------- zobrist ----------------
extern U64 ZPiece[12][64], ZCastle[16], ZEp[8], ZSide;
void init_zobrist();
void init_cuckoo();

// ---------------- position ----------------
struct StateInfo {
    U64 key;
    U64 pawnKey;
    U64 npKey[2];
    U64 checkers;
    U64 pinned;     // pieces of side to move pinned to own king
    int castling;
    int ep;
    int halfmove;
    int captured;   // piece captured by the move that led here
    int pliesFromNull;
};

// NNUE dirty piece record: written by make_move, consumed by evaluator.
struct DirtyPiece {
    int n;
    int piece[3], from[3], to[3];  // from/to = NO_SQ for removal/addition
};

const int MAX_STATES = 4096;

extern bool Chess960;  // UCI_Chess960: castling is written king-takes-rook, FEN uses rook files

// Castling right index r (bit 1 << r): 0 = white O-O, 1 = white O-O-O, 2 = black O-O, 3 = black O-O-O.
// A castling move is encoded king-from -> own-rook-square with MT_CASTLE (works for Chess960 and DFRC).
inline int castle_king_to(int r) { return (r >= 2 ? 56 : 0) + ((r & 1) ? 2 : 6); }
inline int castle_rook_to(int r) { return (r >= 2 ? 56 : 0) + ((r & 1) ? 3 : 5); }

struct Position {
    U64 byType[6];
    U64 byColor[2];
    uint8_t board[64];
    int stm;
    int sp;  // index of current state
    int gamePly;
    StateInfo st[MAX_STATES];
    // castling geometry, fixed for the game (set by set_fen)
    int castleRookSq[4];
    U64 castlePath[4];      // squares that must be empty (excluding king and rook)
    U64 castleKingPath[4];  // squares the king crosses incl. destination (must not be attacked)
    uint8_t castleMask[64];  // rights kept when a piece moves from / to the square
    // NNUE hooks
    DirtyPiece *dirtyOut = nullptr;  // if set, make_move writes into dirtyOut[sp] (indexed by new sp)

    StateInfo &s() { return st[sp]; }
    const StateInfo &s() const { return st[sp]; }

    U64 occ() const { return byColor[0] | byColor[1]; }
    U64 pieces(int c, int pt) const { return byColor[c] & byType[pt]; }
    int king_sq(int c) const { return lsb(pieces(c, KING)); }
    bool in_check() const { return st[sp].checkers != 0; }

    void clear();
    bool set_fen(const std::string &fen);
    void add_castling(int c, int rsq);
    std::string fen() const;

    void put(int p, int s) {
        board[s] = p;
        byType[piece_type(p)] |= sqbb(s);
        byColor[piece_color(p)] |= sqbb(s);
    }
    void remove(int s) {
        int p = board[s];
        byType[piece_type(p)] ^= sqbb(s);
        byColor[piece_color(p)] ^= sqbb(s);
        board[s] = NO_PIECE;
    }
    void move_piece(int f, int t) {
        int p = board[f];
        U64 ft = sqbb(f) | sqbb(t);
        byType[piece_type(p)] ^= ft;
        byColor[piece_color(p)] ^= ft;
        board[t] = p;
        board[f] = NO_PIECE;
    }

    U64 attackers_to(int s, U64 occ) const {
        return (PawnAtt[BLACK][s] & pieces(WHITE, PAWN)) | (PawnAtt[WHITE][s] & pieces(BLACK, PAWN)) |
               (KnightAtt[s] & byType[KNIGHT]) | (KingAtt[s] & byType[KING]) |
               (bishop_attacks(s, occ) & (byType[BISHOP] | byType[QUEEN])) |
               (rook_attacks(s, occ) & (byType[ROOK] | byType[QUEEN]));
    }
    bool sq_attacked(int s, int byC, U64 occ) const {
        return (PawnAtt[byC ^ 1][s] & pieces(byC, PAWN)) || (KnightAtt[s] & pieces(byC, KNIGHT)) ||
               (KingAtt[s] & pieces(byC, KING)) ||
               (bishop_attacks(s, occ) & (pieces(byC, BISHOP) | pieces(byC, QUEEN))) ||
               (rook_attacks(s, occ) & (pieces(byC, ROOK) | pieces(byC, QUEEN)));
    }

    void compute_check_info();
    U64 compute_key() const;
    U64 compute_pawn_key() const;
    U64 compute_np_key(int c) const;

    void generate(GenType gt, MoveList &ml) const;
    bool is_legal(Move m) const;
    bool is_pseudo_legal(Move m) const;
    bool is_capture(Move m) const {
        return (board[move_to(m)] != NO_PIECE && move_type(m) != MT_CASTLE) || move_type(m) == MT_EP;
    }
    int castle_right(Move m) const {  // castling right index of an MT_CASTLE move
        return 2 * stm + (move_to(m) < move_from(m));
    }
    bool is_tactical(Move m) const {
        return is_capture(m) || (move_type(m) == MT_PROMO && move_promo(m) == QUEEN);
    }
    bool gives_check_approx(Move m) const;

    void make_move(Move m);
    void unmake_move(Move m);
    void make_null();
    void unmake_null();

    bool see_ge(Move m, int threshold) const;
    bool is_draw(int ply) const;
    bool upcoming_repetition(int ply) const;  // 50-move + repetition
    bool has_repeated() const;                // any repetition since the last irreversible move
    void copy_from(const Position &o);        // copy position + recent history (for repetition detection)
    bool has_non_pawn(int c) const {
        return byColor[c] & (byType[KNIGHT] | byType[BISHOP] | byType[ROOK] | byType[QUEEN]);
    }
};
