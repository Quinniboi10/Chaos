#pragma once

#include "board.h"
#include "search.h"

// ************ POLICY NETWORK CONFIG ************
constexpr i16   Q_P       = 128;
constexpr usize HL_SIZE_P = 256;

constexpr int ACTIVATION_P = CReLU;

void initPolicy();

void fillPolicy(const Board& board, Tree& tree, const Node& parent, const float temperature);