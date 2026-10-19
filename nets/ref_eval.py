#!/usr/bin/env python3
"""Pure-Python reference evaluator for the erebus midlevel net (quantised.bin).

Slow and dependency-free on purpose: it is the executable form of
docs/INFERENCE.md, used to produce that document's test vectors. Port from the
document, then check your implementation against this script's output.

    python3 docs/ref_eval.py <quantised.bin> ["<fen>" ...]
"""
import struct
import sys

HL, L2, L3 = 1024, 16, 32
IB, OB = 8, 8
QA, QB, SCALE = 255, 64, 400

KING_BUCKETS = [
    0, 1, 2, 3, 3, 2, 1, 0,
    4, 4, 5, 5, 5, 5, 4, 4,
    6, 6, 6, 6, 6, 6, 6, 6,
] + [7] * 40

DEFAULT_FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
    "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 8",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "6k1/5pp1/7p/8/8/7P/5PP1/3R2K1 w - - 0 1",
    "4k3/8/8/8/8/8/8/QQQQK3 b - - 0 1",
]


def load(path):
    data = open(path, "rb").read()
    off = 0

    def take(fmt, n):
        nonlocal off
        size = struct.calcsize("<" + fmt) * n
        vals = struct.unpack_from(f"<{n}{fmt}", data, off)
        off += size
        return list(vals)

    net = {
        "l0w": take("h", IB * 768 * HL),
        "l0b": take("h", HL),
        "l1w": take("h", OB * L2 * 2 * HL),
        "l1b": take("f", OB * L2),
        "l2w": take("f", OB * L3 * L2),
        "l2b": take("f", OB * L3),
        "l3w": take("f", OB * L3),
        "l3b": take("f", OB),
    }
    assert off == 13_128_224, off
    return net


def parse_fen(fen):
    """-> (pieces [(colour 0=W/1=B, type 0..5 PNBRQK, square a1=0)], stm)"""
    board, stm = fen.split()[:2]
    pieces = []
    for r, row in enumerate(board.split("/")):
        rank = 7 - r
        f = 0
        for ch in row:
            if ch.isdigit():
                f += int(ch)
            else:
                pieces.append((0 if ch.isupper() else 1, "pnbrqk".index(ch.lower()), rank * 8 + f))
                f += 1
    return pieces, (0 if stm == "w" else 1)


def feature(persp, ksq_persp, colour, ptype, sq):
    """Input index into l0w for one piece, seen from perspective `persp`."""
    flip = 56 if persp == 1 else 0
    bucket = KING_BUCKETS[ksq_persp ^ flip]
    return bucket * 768 + 384 * (colour != persp) + 64 * ptype + (sq ^ flip)


def accumulator(net, pieces, persp):
    ksq = next(sq for c, t, sq in pieces if c == persp and t == 5)
    acc = list(net["l0b"])
    w = net["l0w"]
    for c, t, sq in pieces:
        base = feature(persp, ksq, c, t, sq) * HL
        for i in range(HL):
            acc[i] += w[base + i]
    return acc


def evaluate(net, fen):
    pieces, stm = parse_fen(fen)
    us, them = accumulator(net, pieces, stm), accumulator(net, pieces, 1 - stm)
    ob = (len(pieces) - 2) // 4

    c = [min(max(v, 0), QA) for v in us + them]
    c2 = [v * v for v in c]
    h2 = []
    for o in range(L2):
        row = (ob * L2 + o) * 2 * HL
        s = sum(c2[i] * net["l1w"][row + i] for i in range(2 * HL))
        x = s / (QA * QA * QB) + net["l1b"][ob * L2 + o]
        h2.append(min(max(x, 0.0), 1.0) ** 2)
    h3 = []
    for j in range(L3):
        row = (ob * L3 + j) * L2
        x = sum(net["l2w"][row + o] * h2[o] for o in range(L2)) + net["l2b"][ob * L3 + j]
        h3.append(min(max(x, 0.0), 1.0) ** 2)
    out = sum(net["l3w"][ob * L3 + j] * h3[j] for j in range(L3)) + net["l3b"][ob]
    return out * SCALE, ob


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    net = load(sys.argv[1])
    for fen in sys.argv[2:] or DEFAULT_FENS:
        cp, ob = evaluate(net, fen)
        print(f"{cp:9.2f} cp  ob={ob}  {fen}")


if __name__ == "__main__":
    main()
