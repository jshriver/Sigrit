// Lazy SMP thread pool and search lifecycle (go / stop / ponderhit).
#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include "search.h"

extern std::vector<std::unique_ptr<Worker>> workers;
extern std::atomic<bool> stopFlag;         // all threads stop as soon as they see it
extern std::atomic<bool> pondering;        // go ponder in progress: time limits are not enforced
extern std::atomic<bool> stopOnPonderhit;  // time ran out while pondering: answer at ponderhit

void set_threads(int n);  // resizes the worker pool
void search_clear();      // ucinewgame
int64_t total_nodes();
int64_t total_tbhits();
// Starts searching pos on a background thread; it prints info and bestmove when done.
// pos must stay unchanged until the search has finished (wait_for_search).
void start_thinking(Position &pos, const Limits &lim, int64_t startTimeMs);
void wait_for_search();
void stop_search();
bool search_running();
void ponderhit();  // switch a ponder search to a normal timed search
