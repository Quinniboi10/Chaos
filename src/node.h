#pragma once

#include "search.h"
#include "ttable.h"

class Node {
    RelaxedAtomic<i64> totalScore_;
    RelaxedAtomic<u8>  giniImpurity_;
    RelaxedAtomic<u8>  policy_;

   public:
    RelaxedAtomic<NodeIndex> firstChild;
    RelaxedAtomic<u64>       visits;
    RelaxedAtomic<Move>      move;
    RelaxedAtomic<GameState> state;
    RelaxedAtomic<u8>        numChildren;

    Node() {
        totalScore_   = 0;
        giniImpurity_ = 0;
        policy_       = 0;
        firstChild    = { 0, 0 };
        visits        = 0;
        move          = Move::null();
        state         = ONGOING;
        numChildren   = 0;
    }

    Node(const Node& other) {
        totalScore_   = other.totalScore_.load();
        giniImpurity_ = other.giniImpurity_.load();
        policy_       = other.policy_.load();
        firstChild    = other.firstChild.load();
        visits        = other.visits.load();
        move          = other.move.load();
        state         = other.state.load();
        numChildren   = other.numChildren.load();
    }

    Node& operator=(const Node& other) {
        if (this != &other) {
            totalScore_   = other.totalScore_.load();
            giniImpurity_ = other.giniImpurity_.load();
            policy_       = other.policy_.load();
            firstChild    = other.firstChild.load();
            visits        = other.visits.load();
            move          = other.move.load();
            state         = other.state.load();
            numChildren   = other.numChildren.load();
        }
        return *this;
    }

    float totalScore() const { return totalScore_ / 32768.0f; }
    void  setTotalScore(const float val) { totalScore_.store(val * 32768); }
    void  incrementTotalScore(const float val) { totalScore_.getUnderlying().fetch_add(val * 32768, std::memory_order_relaxed); }

    float policy() const { return policy_.load() / 255.0f; }
    void  setPolicy(const float val) { policy_.store(val * 255); }

    float giniImpurity() const { return giniImpurity_.load() / 255.0f; }
    void  setGiniImpurity(const float val) { giniImpurity_.store(val * 255); }

    // Get the WDL of a node, adjusted for game end states
    float q() const {
        assert(visits.load());
        return totalScore() / visits.load();
    }

    bool isExpanded() const { return numChildren.load() > 0; }
    bool isTerminal() const { return state.load().state() != ONGOING; }

    bool operator==(const Node& other) const { return visits == other.visits.load() && firstChild.load() == other.firstChild.load(); }
};


class Tree {
    u8 currentHalf;

   public:
    array<vector<Node>, 2> nodes;
    TranspositionTable     tt;
    RelaxedAtomic<bool>    switchHalves;

    Tree() {
        resize(DEFAULT_HASH);
        currentHalf  = 0;
        switchHalves = false;
    }

    void reset() {
        nodes[0][0] = Node();
        nodes[1][0] = Node();
        tt.clear(std::thread::hardware_concurrency());
    }

    void resize(const u64 newMB) {
        // The TT gets 1/16th of the hash
        // and the main tree gets the other
        // 15/16ths
        const u64 treeAllocSize = newMB * 1024 * 1024 * 15 / sizeof(Node) / 16;

        nodes[0].resize(treeAllocSize / 2);
        nodes[1].resize(treeAllocSize / 2);

        tt.reserve(newMB / 16);
        tt.clear(std::thread::hardware_concurrency());
    }

    u8   activeHalf() const { return currentHalf; }
    void switchHalf() { currentHalf ^= 1; }

    Node&       root() { return nodes[currentHalf][0]; }
    const Node& root() const { return nodes[currentHalf][0]; }

    vector<Node>&       activeTree() { return nodes[currentHalf]; }
    const vector<Node>& activeTree() const { return nodes[currentHalf]; }
    vector<Node>&       inactiveTree() { return nodes[currentHalf ^ 1]; }
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