#pragma once

#include "types.h"

constexpr float CPUCT             = 1.75;
constexpr float FPU               = 0.5;
constexpr float TEMPERATURE       = 1.0;
constexpr float EVAL_DIVISOR      = 400;
constexpr float CPUCT_VISIT_SCALE = 8192;

constexpr array<int, 7> PIECE_VALUES = { 100, 316, 328, 493, 982, 0, 0 };