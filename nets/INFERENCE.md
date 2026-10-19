# erebus midlevel NNUE — inference reference

Everything needed to evaluate `quantised.bin` (the final net is
`erebus-mid/checkpoints/erebus-mid-2188/quantised.bin`, identical to the
engine's `nets/net.nnue`) in any language. Derived from `src/main.rs`, the
bullet source it was trained with (commit `2160e65`), and the erebus engine's
`src/nnue.rs`. Checked against the engine: [docs/ref_eval.py](ref_eval.py) is a
dependency-free executable version of this document and matches
`erebus --probe-fen` on every test vector below.

```
net file   SHA-256 2e58d38d86e9032cc32a73bfea6429f59a25bbe0dd949380b7cc1d7bd86e4da9
           13,128,256 bytes (13,128,224 data + 32 bytes "bullet" padding)
```

## 1. Architecture

```
            white-persp. features ─┐                ┌─ black-persp. features
                                   ▼                ▼
   768 x 8 king buckets ──► acc_white[1024]    acc_black[1024]   (i16, shared weights l0w/l0b)
                                   │                │
              order by side to move: [acc_stm (1024) | acc_ntm (1024)]
                                   │
                    SCReLU ──► 2048 ──l1w[ob]──► 16  ──SCReLU──► 16
                                       ──l2w[ob]──► 32  ──SCReLU──► 32
                                       ──l3w[ob]──► 1   ──× 400──► centipawns (side to move)
```

| constant | value | meaning |
|---|---|---|
| `HL` | 1024 | accumulator width per perspective |
| `INPUT_BUCKETS` | 8 | king buckets (copies of the 768 input weights) |
| `OUTPUT_BUCKETS` | 8 | material buckets for every layer after the accumulator |
| `L2` | 16 | first hidden layer after the accumulator |
| `L3` | 32 | second hidden layer |
| `QA` | 255 | quantisation of `l0w`, `l0b` |
| `QB` | 64 | quantisation of `l1w` |
| `SCALE` | 400 | output → centipawns |

SCReLU(x) = clamp(x, 0, 1)². In integer accumulator units, clamp to `0..QA`.

The output is from the **side to move's** point of view (positive = good for
the side to move), like any negamax eval.

## 2. File layout

All little-endian, packed, no header. `ob` = output bucket (outermost index
of every post-accumulator block).

| block | type | shape (row-major, last index fastest) | elements | byte offset | scale |
|---|---|---|---|---|---|
| `l0w` | i16 | `[8 king bucket][768 feature][1024]` | 6,291,456 | 0 | × QA |
| `l0b` | i16 | `[1024]` | 1,024 | 12,582,912 | × QA |
| `l1w` | i16 | `[8 ob][16 out][2048 in]` | 262,144 | 12,584,960 | × QB |
| `l1b` | f32 | `[8 ob][16]` | 128 | 13,109,248 | float |
| `l2w` | f32 | `[8 ob][32 out][16 in]` | 4,096 | 13,109,760 | float |
| `l2b` | f32 | `[8 ob][32]` | 256 | 13,126,144 | float |
| `l3w` | f32 | `[8 ob][32]` | 256 | 13,127,168 | float |
| `l3b` | f32 | `[8 ob]` | 8 | 13,128,192 | float |
| padding | ascii | `"bulletbullet…"` to 64-byte boundary | 32 bytes | 13,128,224 | ignore |

Notes:
- `l0w` row for input `f` (0 ≤ f < 6144) is the 1024 contiguous i16 at
  element `f * 1024`, where `f = 768 * king_bucket + feature`.
- `l1w` input index `i < 1024` is the side-to-move accumulator, `i ≥ 1024`
  the other side.
- The training-time factoriser (`l0f`) is already summed into `l0w`; the
  engine never sees it.
- The f32 blocks start 4-byte aligned. A loader should check total size ≥
  13,128,224 and may reject a net whose weights are all zero.

## 3. Board features

Squares: a1 = 0, b1 = 1, …, h1 = 7, a2 = 8, …, h8 = 63 (`sq = 8*rank + file`).
Piece types: pawn 0, knight 1, bishop 2, rook 3, queen 4, king 5 (kings are
inputs too). Colours: white 0, black 1.

Each **perspective** `P` (white or black) has its own accumulator. For every
piece on the board (all of them, up to 32), perspective `P` activates one
input:

```
flip(P)      = 0 if P is white, 56 if P is black      # vertical flip, NOT horizontal
rel_colour   = 0 if piece colour == P else 1          # "ours" vs "theirs"
rel_sq       = sq ^ flip(P)
feature      = 384 * rel_colour + 64 * piece_type + rel_sq      # 0..767
king_bucket  = KING_BUCKETS[ own_king_sq(P) ^ flip(P) ]
input        = 768 * king_bucket + feature                       # 0..6143
```

So each side sees the board from its own side, with its own pieces in the
first 384 slots. **There is no horizontal mirroring**: the bucket layout is
left/right symmetric, but the features are not mirrored.

King bucket table (index = king square from that perspective's own view):

```
KING_BUCKETS = [
    0, 1, 2, 3, 3, 2, 1, 0,     # rank 1   (a1..h1)
    4, 4, 5, 5, 5, 5, 4, 4,     # rank 2
    6, 6, 6, 6, 6, 6, 6, 6,     # rank 3
    7, 7, 7, 7, 7, 7, 7, 7,     # ranks 4..8
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7,
]
```

Example: black king on g8 (sq 62) → from black's view 62 ^ 56 = 6 → bucket 1.

Output bucket (shared by both perspectives, kings counted):

```
ob = (number_of_pieces_on_board - 2) / 4       # integer division, 0..7
```

(bullet `MaterialCount<8>`: divisor = ceil(32 / 8) = 4.)

## 4. Accumulators

```
acc_P[j] = l0b[j] + Σ_{active inputs f of P} l0w[f][j]        j = 0..1023, i16
```

i16 is enough: each `l0w` was clipped to ±1.98 × 255 ≈ ±505 during
training, and a real position has at most 32 active inputs.

Incremental updates (the speed trick; optional for a first port):
- A quiet move: subtract the piece's old input, add its new one (per perspective).
- A capture: also subtract the captured piece. Promotion: subtract pawn, add
  the promoted piece. Castling: king and rook both move. En passant: the
  captured pawn is not on the destination square.
- **If a perspective's own king moves into a different king bucket, that
  perspective's accumulator must be rebuilt from scratch** (every input index
  of that side changed). The other perspective is updated normally. A king
  move inside the same bucket is an ordinary update. (The engine keeps a
  per-bucket cache of accumulators, "finny table" style, to make rebuilds
  cheap; a plain refresh is correct too.)
- Rebuilding from scratch always gives the same result as incremental updates
  (integer adds), which is a good debug check.

## 5. Forward pass

```
(us, them) = (acc_white, acc_black) if white to move else (acc_black, acc_white)
ob = output_bucket(piece_count)

# layer 1: 2048 -> 16, integer
c[i]  = clamp(us[i], 0, QA)            for i in 0..1023
c[i]  = clamp(them[i-1024], 0, QA)     for i in 1024..2047
s[o]  = Σ_{i=0..2047} c[i] * c[i] * l1w[ob][o][i]          # accumulate in i64
x[o]  = s[o] / (QA*QA*QB) + l1b[ob][o]                     # float; QA*QA*QB = 4,161,600
h2[o] = clamp(x[o], 0, 1)^2                                # o = 0..15

# layer 2: 16 -> 32, float
h3[j] = clamp(l2b[ob][j] + Σ_o l2w[ob][j][o] * h2[o], 0, 1)^2      # j = 0..31

# layer 3: 32 -> 1, float
out   = l3b[ob] + Σ_j l3w[ob][j] * h3[j]
cp    = trunc(out * 400)          # engine: (out * 400.0f) as i32, rounds toward zero
```

Numeric details:
- **Overflow:** one term of `s` can be 255² × 127 ≈ 8.3 M and there are 2048
  terms, so the worst case (≈1.7 × 10¹⁰) overflows i32. Accumulate in i64,
  or in i32 and flush to i64/f64 every ~200 inputs.
- `c * l1w` (not squared) fits i16 (255 × 127 < 32767) because `l1w` is
  within ±127 (±1.98 × 64). The usual SIMD trick: compute `(c * w) as i16`,
  then `madd` with `c` into i32 lanes (`_mm256_madd_epi16` / `i32x4.dot_i16x8`).
- The erebus engine instead stores `sq = (c*c) >> 1` (≤ 32512, fits i16) and
  multiplies the sum by `2 / (QA*QA*QB)`. That drops one low bit per term:
  results are the same to the centipawn on all test vectors.
- Sparsity: many `c[i]` are 0 after the clamp, so skipping zero inputs (or
  zero pairs) of layer 1 is a big win; layer 1 is ~33 k multiply-adds and
  dominates the per-eval cost after the accumulator.
- Use f32 for layers 2–3 to match the engine bit-for-bit; f64 is fine if you
  only need cp-level agreement.

## 6. Test vectors

Final net (SHA-256 above). "ref" = [ref_eval.py](ref_eval.py) (exact i64 sum,
f64 floats); "engine" = `erebus --probe-fen`; "float" = the trainer's
unquantised net at superbatch 2188 (from the training log), for reference only.

| FEN | pieces | ob | ref (cp) | engine | float |
|---|---|---|---|---|---|
| `rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1` | 32 | 7 | 47.40 | 47 | 49 |
| `r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1` | 32 | 7 | 738.55 | 738 | 751 |
| `r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 8` | 30 | 7 | -1191.66 | -1191 | -1187 |
| `8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1` | 10 | 2 | 64.26 | 64 | 62 |
| `6k1/5pp1/7p/8/8/7P/5PP1/3R2K1 w - - 0 1` | 9 | 1 | 1912.59 | 1912 | 1977 |
| `4k3/8/8/8/8/8/8/QQQQK3 b - - 0 1` | 6 | 1 | -2048.20 | -2048 | -2065 |

A new implementation should match the "engine" column exactly (or the "ref"
column within ±1 cp). The "float" column differs by quantisation error (a few
cp in the middlegame, tens of cp in lopsided endgames) and is only a sanity
check that the file was trained by this trainer.

Note the second vector is black to move: it tests the perspective swap. If
it is wrong while the white-to-move ones are right, check the stm ordering of
`[us | them]` and the `^ 56` flip.

## 7. Common bugs

| symptom | likely cause |
|---|---|
| every position gives the same value | reading the wrong offsets; l1 sum always 0 |
| startpos far from +47 | wrong feature formula, piece order, or `l0w` row stride |
| white-to-move right, black-to-move wrong | `us`/`them` not ordered by side to move, or black not flipped with `^ 56` |
| right until a king walks, then drifts | king-bucket change not triggering a rebuild |
| correct in opening, wrong in endgames | output bucket formula (kings must be counted) |
| huge / garbage values in busy positions | i32 overflow in the layer-1 sum |
| black perspective king bucket wrong | used `KING_BUCKETS[ksq]` instead of `KING_BUCKETS[ksq ^ 56]` |
| mirror-image positions disagree | mirroring files (`^ 7`) — this net does not mirror |

## 8. Porting checklist

1. Load: read the 8 blocks at the offsets in §2 (check size and the hash).
2. Features: `input(P, piece, sq)` from §3; unit-test the g8 king example.
3. Accumulators: full refresh from a FEN; then incremental updates with a
   rebuild on own-king bucket change; assert incremental == refresh in a
   perft-style walk.
4. Forward pass from §5 with i64 layer-1 sums.
5. Match the §6 table. Then optimise (SIMD, sparse layer 1, accumulator cache)
   and re-match.
