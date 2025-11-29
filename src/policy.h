#pragma once

#include "node.h"
#include "board.h"
#include "searcher.h"

// ************ POLICY NETWORK CONFIG ************
constexpr i16   Q_P       = 128;
constexpr usize HL_SIZE_P = 1024;

constexpr int ACTIVATION_P = CReLU;

void initPolicy();
void fillPolicy(const Board& board, Tree& tree, const SearcherData* searcherData, Node& parent, const float temperature);