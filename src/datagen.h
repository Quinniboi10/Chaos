#pragma once

#include "types.h"

namespace datagen {
constexpr usize OUTPUT_BUFFER_GAMES = 50;
constexpr usize RAND_MOVES          = 8;
constexpr usize HASH_PER_T          = 128;

constexpr double TEMPERATURE = 1.4;

void run(const string& params);
}