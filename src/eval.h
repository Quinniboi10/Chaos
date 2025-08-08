#pragma once

#include "board.h"

// ************ NETWORK CONFIG ************
constexpr i16   QA             = 255;
constexpr i16   QB             = 64;
constexpr i16   EVAL_SCALE     = 400;
constexpr usize HL_SIZE        = 32;

constexpr int ReLU   = 0;
constexpr int CReLU  = 1;
constexpr int SCReLU = 2;

constexpr int ACTIVATION = CReLU;

i32 evaluate(const Board& board);