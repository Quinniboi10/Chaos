#pragma once

#include "types.h"

constexpr float CPUCT                      = 1.1;
constexpr float ROOT_CPUCT                 = 2.4;
constexpr float POLICY_TEMPERATURE         = 0.75;
constexpr float EG_POLICY_TEMPERATURE      = 0.6;
constexpr float ROOT_POLICY_TEMPERATURE    = 2.9;
constexpr float EG_ROOT_POLICY_TEMPERATURE = 2.5;
constexpr float EVAL_DIVISOR               = 400;
constexpr float CPUCT_VISIT_SCALE          = 8192;

constexpr i32 POLICY_MATERIAL_PHASE_DIVISOR = 4500;
constexpr i32 POLICY_EVAL_PHASE_DIVISOR     = 1500;

constexpr i32   BUTTERFLY_BONUS_DIVISOR  = 8192;
constexpr float BUTTERFLY_POLICY_DIVISOR = 16384;

constexpr float GINI_BASE   = 0.45;
constexpr float GINI_SCALAR = 1.6;
constexpr float GINI_MIN    = 1.0;
constexpr float GINI_MAX    = 1.5;

constexpr array PIECE_VALUES = { 100, 316, 328, 493, 982, 0, 0 };