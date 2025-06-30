#pragma once

#include "types.h"
#include "board.h"
#include "search.h"

struct Worker {
    atomic<u64> nodes;

    void search(const Board& board, vector<Node>& nodes, const SearchParameters params, const SearchLimits limits);
};