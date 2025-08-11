#pragma once

#include <limits>

#include "types.h"

// Using ms
constexpr usize UCI_REPORTING_FREQUENCY = 1000;
constexpr usize MOVE_OVERHEAD           = 20;

constexpr u64 INF_U64 = std::numeric_limits<u64>::max();
constexpr u64 INF_U32 = std::numeric_limits<u32>::max();
constexpr int INF_I16 = std::numeric_limits<i16>::max();
constexpr int INF_INT = std::numeric_limits<int>::max();

constexpr u64 LIGHT_SQ_BB = 0x55AA55AA55AA55AA;
constexpr u64 DARK_SQ_BB  = 0xAA55AA55AA55AA55;

// ************ DEFAULT UCI OPTIONS ************
constexpr usize DEFAULT_HASH = 16;