#pragma once

#include "types.h"
#include "move.h"
#include "tunable.h"

constexpr usize SCORE_QUANTIZATION = 1024;

constexpr i32 MATE_SCORE = 32767;

inline double cpToWDL(int cp) { return sigmoid((static_cast<double>(cp) / EVAL_DIVISOR)); }
inline i32 wdlToCP(double wdl) {
    assert(wdl > 0);
    assert(wdl < 1);
    return inverseSigmoid(wdl) * EVAL_DIVISOR;
}

struct Node {
    atomic<i64> totalScore;
    atomic<u64> visits;
    atomic<u64> firstChild;
    atomic<GameState> state;
    atomic<Move> move;
    atomic<u8> numChildren;
    Node* parent;

    Node() {
        totalScore = 0;
        visits = 0;
        firstChild = 0;
        state = ONGOING;
        move = Move::null();
        numChildren = 0;
        parent = nullptr;
    }

    Node(const Node& other) {
        totalScore = other.totalScore.load();
        visits = other.visits.load();
        firstChild = other.firstChild.load();
        state = other.state.load();
        move = other.move.load();
        numChildren = other.numChildren.load();
        parent = other.parent;
    }

    Node& operator=(const Node& other) {
        if (this != &other) {
            totalScore = other.totalScore.load();
            visits = other.visits.load();
            firstChild = other.firstChild.load();
            state = other.state.load();
            move = other.move.load();
            numChildren = other.numChildren.load();
            parent = other.parent;
        }
        return *this;
    }

    double getScore() const {
        if (state == ONGOING)
            return static_cast<double>(totalScore) / SCORE_QUANTIZATION / (visits + 1);
        if (state == DRAW)
            return 0;
        if (state == WIN)
            return 1;
        return -1;
    }
};

struct SearchParameters {
    double cpuct;

    bool doReporting;

    SearchParameters(double cpuct, bool doReporting) : cpuct(cpuct), doReporting(doReporting) {}
};

struct SearchLimits {
    u64 maxNodes;
    usize depth;

    SearchLimits(const u64 hash, const usize depth, const u64 nodes) : depth(depth) {
        maxNodes = hash * 1024 * 1024 / sizeof(Node);
        if (nodes)
            maxNodes = std::max(maxNodes, nodes);
    }
};