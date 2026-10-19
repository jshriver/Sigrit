# Sigrit

Sigrit is a UCI chess engine written in C++17. It pairs an NNUE evaluation with a modern
alpha-beta search, and supports multi-threading, Syzygy tablebases, Chess960 and pondering.
It is a command-line program with no board of its own: use it from a UCI chess GUI such as
Cute Chess, Arena, Banksia or En Croissant.

Im a big fan of En Croissant.

## Features

### Search
- Principal variation search with iterative deepening and aspiration windows
- Transposition table: 3-entry buckets, generation-based replacement, huge-page backed on Linux
- Lazy SMP multi-threading (shared transposition table), with the final move chosen by a vote across threads
- Pruning: null-move pruning, reverse futility pruning, razoring, futility pruning,
  late move pruning, history pruning, SEE pruning, probcut, mate distance pruning
- Reductions and extensions: late move reductions, internal iterative reductions,
  singular extensions (including double and negative extensions)
- Quiescence search with SEE and futility pruning
- Move ordering: staged move picker (TT move, good captures, killers, counter move,
  quiets, bad captures), main/butterfly, capture, pawn and continuation histories
- Static evaluation correction history (pawn structure, non-pawn material, continuation)
- Repetition detection, including upcoming repetitions (cuckoo tables)
- Time management that adapts to best-move stability, score drops and the share of nodes
  spent on the best move

### Evaluation
- NNUE: 768 inputs × 8 king buckets → 1024×2 → 16 → 32 → 1, with 8 output buckets and SCReLU activations
- Incremental accumulator updates, with a refresh cache for king-bucket changes
- Sparse first hidden layer (skips inputs that are zero)
- AVX2 SIMD code on x86-64; portable C++ that the compiler vectorizes (NEON) on ARMv8.
  Every build gives bit-identical evaluations
- The network is embedded in the binary, so no separate file is needed at runtime

### Variants and protocol
- UCI protocol
- Chess960 and Double Fischer Random Chess (X-FEN and Shredder-FEN castling rights)
- Pondering (`go ponder` / `ponderhit`)
- Syzygy endgame tablebases up to 7 pieces via [Fathom](https://github.com/jdart1/Fathom):
  - DTZ-based root move ranking, with a WDL fallback
  - WDL probes during search

## Requirements

- One of:
  - an x86-64 CPU with BMI2 and AVX2 (x86-64-v3: Intel Haswell or newer, AMD Zen 3 or newer).
    AMD Zen 1/2 CPUs can run it, but slowly, because their PEXT instruction is slow.
  - a 64-bit ARM CPU (ARMv8: Apple Silicon, Raspberry Pi 4/5, AWS Graviton, Snapdragon).
- Linux, macOS, or Windows (built with [MSYS2](https://www.msys2.org/)).
- GCC or clang with C++17 support, and GNU make.

Prebuilt binaries for every platform are attached to each
[GitHub release](https://github.com/jshriver/Sigrit/releases).

## Building

```sh
cd src
make -j
```

This produces `src/sigrit` (`sigrit.exe` on Windows): a binary with LTO, the network
embedded, and code tuned for the CPU it is built on. It is statically linked except on macOS,
which does not support that. The build prints what it detected:

```
🔍 Building Sigrit 1.0 for native (haswell, x86-64-v3 class) on linux with gcc
```

On Windows, build from an MSYS2 UCRT64 shell (`pacman -S make mingw-w64-ucrt-x86_64-gcc`),
or CLANGARM64 on ARM (`make CXX=clang++ CC=clang`).

Intermediate files go to `src/build/`, which is removed when the build finishes.

### Targets

| Command | Result |
|---|---|
| `make` | Optimized build for this CPU |
| `make pgo` | Profile-guided build: instruments, runs `bench`, rebuilds using the profile (GCC only) |
| `make debug` | `sigrit-debug`, built with `-O1 -g`, AddressSanitizer and UBSan |
| `make perft` | `perft_test`, the move generator test tool |
| `make bench` | Builds, then runs the benchmark |
| `make clean` | Removes all build output |
| `make print-arch` | Prints the best ARCH this machine can run |
| `make help` | Lists targets and options |

### Options

| Variable | Default | Meaning |
|---|---|---|
| `ARCH` | `native` | `native` (detect this CPU), `x86-64-v3` (AVX2 + BMI2), `x86-64-v4` (AVX-512), or `armv8`. Use a named ARCH when building for a different machine. `x86-64-bmi2` and `x86-64-avx512` are accepted as aliases |
| `TUNE` | none | Adds `-mtune=<cpu>` |
| `EXE` | `sigrit` | Output file name |
| `NET` | `../nets/net.nnue` | Network to embed |
| `VERSION` | from git | Overrides the version string |
| `STATIC` | `yes` (`no` on macOS) | Static linking |
| `LTO` | `yes` | Link-time optimization |
| `BENCH_DEPTH` | `13` | Depth used by `make bench` and `make pgo` |
| `V` | | `V=1` shows the full compiler commands |

Example: a build for other AVX2 + BMI2 machines:

```sh
make ARCH=x86-64-v3 EXE=sigrit-v3
```

Options are read from the command line only; same-named environment variables are ignored.

### Versioning

The version comes from `git describe --tags`, with any leading `v` dropped, and appears in
the engine's `id name`:

| Repository state | Engine name |
|---|---|
| On tag `v1.2` | `Sigrit 1.2` |
| 3 commits after the tag | `Sigrit 1.2-3-gabc1234` |
| Uncommitted changes | `-dirty` appended |
| No tags | short commit hash |
| Not a git checkout | `Sigrit dev` |

To make a release, tag the commit and push the tag (see below).

## Continuous integration and releases

[`.github/workflows/build.yml`](.github/workflows/build.yml) runs on every push to `main`,
on pull requests, and on version tags. It builds 9 binaries:

| | x86-64-v3 | x86-64-v4 | armv8 |
|---|---|---|---|
| Linux | ✓ | ✓ | ✓ |
| Windows | ✓ | ✓ | ✓ |
| macOS | ✓ | ✓ | ✓ (Apple Silicon) |

Each job then checks perft (standard and Chess960) and runs `bench`, when the runner's CPU can
execute that build. A final job checks that every tested build reports the same bench node
count, so all platforms and instruction sets play identically.

Pushing a tag that starts with `v` also publishes a GitHub release with all 9 packages
attached (`.tar.gz` for Linux and macOS, `.zip` for Windows):

```sh
git tag v1.2
git push origin v1.2
```

## UCI options

| Option | Type | Default | Description |
|---|---|---|---|
| `Hash` | spin | 256 | Transposition table size in MB (1–65536) |
| `Threads` | spin | 1 | Number of search threads (1–1024) |
| `Move Overhead` | spin | 30 | Milliseconds reserved per move for GUI and network lag |
| `Ponder` | check | false | Lets the GUI use pondering |
| `UCI_Chess960` | check | false | Chess960 mode; castling is written as king-takes-rook (e.g. `e1h1`) |
| `SyzygyPath` | string | `<empty>` | Tablebase directories, separated by `:` (`;` on Windows) |
| `SyzygyProbeDepth` | spin | 1 | Minimum depth for tablebase probes during search |
| `Syzygy50MoveRule` | check | true | Treat wins and losses that the 50-move rule turns into draws as draws |
| `SyzygyProbeLimit` | spin | 7 | Maximum number of pieces to probe |

Setting `SyzygyPath` reports what was found:

```
info string Syzygy: found 510 WDL and 510 DTZ files, up to 6-piece tablebases
```

Tablebase wins and losses appear as `cp 20000` / `cp -20000` (minus the distance in plies).
Actual mates appear as `mate N`.

## Extra commands

Besides the standard UCI commands, the engine accepts these from a terminal:

| Command | Description |
|---|---|
| `bench [depth]` | Fixed-depth search over a built-in set of positions; prints total nodes and nodes per second. Also works as `./sigrit bench 13` from the shell |
| `perft <depth>` | Counts the leaf nodes of the current position at that depth |
| `d` | Prints the current position as a FEN |
| `eval` | Prints the network's static evaluation of the current position |

The bench node count is a quick check that a change did not alter search behaviour.

### Move generator testing

`perft_test` checks perft counts from an EPD file. Each line has a FEN followed by expected
counts in the form `;D<depth> <nodes>`:

```sh
make perft
./perft_test positions.epd 5
```

```
rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 ;D1 20 ;D2 400 ;D3 8902
```

## Source layout

| File | Contents |
|---|---|
| `main.cpp` | Startup: attack tables, Zobrist keys, network, then the UCI loop |
| `uci.cpp/.h` | UCI command loop, options, move and score notation |
| `thread.cpp/.h` | Lazy SMP thread pool and search lifecycle (go / stop / ponderhit) |
| `search.cpp/.h` | Alpha-beta search, quiescence search, iterative deepening |
| `movepick.cpp/.h` | Staged move picker |
| `history.h` | Move-ordering history tables and the search stack |
| `tt.cpp/.h` | Transposition table |
| `timeman.cpp/.h` | Time management |
| `syzygy.cpp/.h` | Syzygy tablebase integration |
| `nnue.cpp/.h` | NNUE inference |
| `position.cpp/.h` | Board state, FEN, make/unmake, SEE, draw detection |
| `movegen.cpp/.h` | Move generation, legality checks, perft |
| `bitboard.cpp/.h` | Attack tables: PEXT sliders with BMI2, magic bitboards otherwise |
| `types.h` | Basic types, move encoding, score constants |
| `bench.cpp/.h` | Benchmark |
| `fathom/` | Fathom Syzygy probing library |
| `../nets/` | The network, plus its format reference (`INFERENCE.md`, `ref_eval.py`) |

## Credits

- Syzygy probing uses [Fathom](https://github.com/jdart1/Fathom) (MIT license), based on
  Ronald de Man's original probing code.

## License

Sigrit is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
