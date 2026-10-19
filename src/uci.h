// UCI protocol: command loop, options, move and score notation.
#pragma once
#include <string>
#include "position.h"

void uci_loop(int argc, char **argv);
void uci_out(const std::string &s);  // thread-safe line output

std::string move_to_uci(Move m);  // honours UCI_Chess960
std::string move_to_uci(Move m, bool c960);
Move parse_uci_move(Position &pos, const std::string &s);
std::string uci_score(int s);  // "cp N" / "mate N"
