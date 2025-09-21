#pragma once

#include "types.h"

constexpr float CPUCT              = 1.1;
constexpr float ROOT_CPUCT         = 1.1;
constexpr float TEMPERATURE        = 1.0;
constexpr float EVAL_DIVISOR       = 400;
constexpr float CPUCT_VISIT_SCALE  = 8192;
constexpr float Q_DECLINING_FACTOR = 0.1;

constexpr array<int, 7> PIECE_VALUES = { 100, 316, 328, 493, 982, 0, 0 };
