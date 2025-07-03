#pragma once

#include "types.h"
#include "move.h"
#include "stopwatch.h"
#include "tunable.h"

constexpr i32 MATE_SCORE = 32767;

inline double cpToWDL(int cp) { return sigmoid((static_cast<double>(cp) / EVAL_DIVISOR)); }
inline i32 wdlToCP(double wdl) {
    assert(wdl > -1);
    assert(wdl < 1);
    return inverseSigmoid(wdl) * EVAL_DIVISOR;
}

struct Node {
    atomic<double> totalScore;
    atomic<u64> visits;
    atomic<u64> firstChild;
    atomic<double> policy;
    atomic<Move> move;
    atomic<GameState> state;
    atomic<u8> numChildren;
    Node* parent;

    Node() {
        totalScore = 0;
        visits = 0;
        firstChild = 0;
        policy = 0;
        state = ONGOING;
        move = Move::null();
        numChildren = 0;
        parent = nullptr;
    }

    Node(const Node& other) {
        totalScore = other.totalScore.load();
        visits = other.visits.load();
        firstChild = other.firstChild.load();
        policy = other.policy.load();
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

    bool operator==(const Node& other) const {
        return totalScore == other.totalScore.load()
            && visits == other.visits.load()
            && firstChild == other.firstChild.load()
            && state == other.state.load()
            && move == other.move.load()
            && numChildren == other.numChildren.load()
            && parent == other.parent;
    }

    double getScore() const {
        if (state == DRAW)
            return 0;
        if (state == WIN)
            return 1;
        if (state == LOSS)
            return -1;
        if (visits == 0)
            return FPU;
        return static_cast<double>(totalScore.load()) / visits.load();
    }
};

struct SearchParameters {
    const vector<u64>& positionHistory;
    double cpuct;

    bool doReporting;

    SearchParameters(const vector<u64>& positionHistory, const double cpuct, const bool doReporting) : positionHistory(positionHistory), cpuct(cpuct), doReporting(doReporting) {}
};

struct SearchLimits {
    Stopwatch<std::chrono::milliseconds> commandTime;
    u64 maxNodes;
    usize depth;
    i64 time;
    i64 inc;

    SearchLimits(const Stopwatch<std::chrono::milliseconds>& commandTime, const u64 hash, const usize depth, const u64 nodes, const i64 time, const i64 inc) {
        this->commandTime = commandTime;
        this->depth = depth;
        this->time = time;
        this->inc = inc;

        maxNodes = hash * 1024 * 1024 / sizeof(Node);
        if (nodes)
            maxNodes = std::min(maxNodes, nodes);
    }
};