#pragma once

#include "board.h"

// ************ VALUE NETWORK CONFIG ************
constexpr i16   QA_V         = 255;
constexpr i16   QB_V         = 64;
constexpr i16   EVAL_SCALE_V = 400;
constexpr usize HL_SIZE_V    = 1024;

constexpr int ACTIVATION_V = SCReLU;

i32 evaluate(const Board& board);
