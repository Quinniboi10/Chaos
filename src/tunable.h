#pragma once

#include "types.h"

constexpr float CPUCT                   = 1.1;
constexpr float ROOT_CPUCT              = 2.4;
constexpr float POLICY_TEMPERATURE      = 1.0;
constexpr float ROOT_POLICY_TEMPERATURE = 3.3;
constexpr float EVAL_DIVISOR            = 400;
constexpr float CPUCT_VISIT_SCALE       = 8192;

constexpr float GINI_BASE = 0.45;
constexpr float GINI_SCALAR = 1.6;
constexpr float GINI_MIN = 1.0;
constexpr float GINI_MAX = 1.5;

constexpr array<int, 7> PIECE_VALUES = { 100, 316, 328, 493, 982, 0, 0 };
