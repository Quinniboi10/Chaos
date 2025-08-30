#pragma once

#include "types.h"

namespace datagen {
constexpr usize OUTPUT_BUFFER_GAMES = 50;
constexpr usize RAND_MOVES          = 8;
constexpr usize HASH_PER_T          = 128;

constexpr double TEMPERATURE = 1.05;

constexpr i32 MAX_STARTPOS_SCORE = 400;

constexpr u64 GENFENS_VERIF_NODES = 2'000;

constexpr u64 POSITION_COUNT_BUFFER = 1024;

void run(const string& params);
void genFens(const string& params);
}