#pragma once

#include "types.h"
#include "constants.h"

#ifdef __AVX512F__
constexpr usize ALIGNMENT = 64;
#else
constexpr usize ALIGNMENT = 32;
#endif

using Accumulator = array<i16, HL_SIZE>;

struct AccumulatorPair {
    alignas(ALIGNMENT) Accumulator white;
    alignas(ALIGNMENT) Accumulator black;

    AccumulatorPair(const Board& board) { load(board); }

    void load(const Board& board);

    bool operator==(const AccumulatorPair& other) const { return white == other.white && black == other.black; }
};