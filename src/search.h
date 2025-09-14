#pragma once

#include "constants.h"
#include "types.h"
#include "move.h"
#include "stopwatch.h"

constexpr i32 MATE_SCORE = 32767;

class NodeIndex {
    u64 idx;

   public:
    NodeIndex() = default;
    NodeIndex(const u64 idx, const u8 half) { this->idx = idx | (static_cast<u64>(half) << 63); }

    u64 index() const { return idx & ~(1ULL << 63); }
    u8  half() const { return idx >> 63; }

    bool operator==(const NodeIndex& other) const { return idx == other.idx; }
};
struct Node {
    RelaxedAtomic<float>     totalScore;
    RelaxedAtomic<NodeIndex> firstChild;
    RelaxedAtomic<u64>       visits;
    RelaxedAtomic<float>     policy;
    RelaxedAtomic<Move>      move;
    RelaxedAtomic<GameState> state;
    RelaxedAtomic<u8>        numChildren;

    Node() {
        totalScore  = 0;
        visits      = 0;
        firstChild  = { 0, 0 };
        policy      = 0;
        state       = ONGOING;
        move        = Move::null();
        numChildren = 0;
    }

    Node(const Node& other) {
        totalScore  = other.totalScore.load();
        visits      = other.visits.load();
        firstChild  = other.firstChild.load();
        policy      = other.policy.load();
        state       = other.state.load();
        move        = other.move.load();
        numChildren = other.numChildren.load();
    }

    Node& operator=(const Node& other) {
        if (this != &other) {
            totalScore  = other.totalScore.load();
            firstChild  = other.firstChild.load();
            visits      = other.visits.load();
            state       = other.state.load();
            policy      = other.policy.load();
            move        = other.move.load();
            state       = other.state.load();
            numChildren = other.numChildren.load();
        }
        return *this;
    }

    bool operator==(const Node& other) const { return visits == other.visits.load() && firstChild.load() == other.firstChild.load(); }

    float getScore() const {
        const GameState s = state.load();
        const u64       v = visits.load();

        if (s == DRAW)
            return 0;
        if (s == WIN)
            return 1;
        if (s == LOSS)
            return -1;
        if (v == 0)
            return 0;
        return totalScore.load() / v;
    }

    float getScore(const u64 visits) const {
        const GameState s = state.load();

        assert(visits > 0);

        if (s == DRAW)
            return 0;
        if (s == WIN)
            return 1;
        if (s == LOSS)
            return -1;
        return totalScore.load() / visits;
    }
};

struct SearchParameters {
    const vector<u64>& positionHistory;
    float              cpuct;
    float              rootCPUCT;
    float              temp;

    bool doReporting;
    bool doUci;

    SearchParameters(const vector<u64>& positionHistory, const float rootCPUCT, const float cpuct, const float temp, const bool doReporting, const bool doUci) :
        positionHistory(positionHistory),
        rootCPUCT(rootCPUCT),
        cpuct(cpuct),
        temp(temp),
        doReporting(doReporting),
        doUci(doUci) {}
};

struct SearchLimits {
    Stopwatch<std::chrono::milliseconds> commandTime;
    u64                                  nodes;
    i64                                  time;
    i64                                  inc;
    usize                                depth;

    SearchLimits(const Stopwatch<std::chrono::milliseconds>& commandTime, const usize depth, const u64 nodes, const i64 time, const i64 inc) {
        this->commandTime = commandTime;
        this->depth       = depth;
        this->nodes       = nodes;
        this->time        = time;
        this->inc         = inc;
    }
};

class Tree {
    u8 currentHalf;

   public:
    array<vector<Node>, 2> nodes;

    Tree() {
        resize(DEFAULT_HASH);
        currentHalf = 0;
    }

    void resize(const u64 size) {
        nodes[0].resize(size / 2);
        nodes[1].resize(size / 2);
    }

    u8 activeHalf() const { return currentHalf; }
    void switchHalf() { currentHalf ^= 1; }

    Node&       root() { return nodes[currentHalf][0]; }
    const Node& root() const { return nodes[currentHalf][0]; }

    vector<Node>& activeTree() { return nodes[currentHalf]; }
    const vector<Node>& activeTree() const { return nodes[currentHalf]; }
    vector<Node>& inactiveTree() { return nodes[currentHalf ^ 1]; }
    const vector<Node>& inactiveTree() const { return nodes[currentHalf ^ 1]; }

    const Node& operator[](const NodeIndex& idx) const {
        assert(idx.index() < nodes[0].size());
        return nodes[idx.half()][idx.index()];
    }

    Node& operator[](const NodeIndex& idx) {
        assert(idx.index() < nodes[0].size());
        return nodes[idx.half()][idx.index()];
    }
};