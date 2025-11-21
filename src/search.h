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

struct SearchParameters {
    const vector<u64>& posHistory;
    float              rootCpuct;
    float              cpuct;
    float              policyTemp;
    float              rootPolicyTemp;

    bool doReporting;
    bool doUci;
    bool minimalUci;

    SearchParameters(const vector<u64>& posHistory, const float rootCpuct, const float cpuct, const float rootPolicyTemp, const float policyTemp, const bool doReporting, const bool doUci, const bool minimalUci) :
        posHistory(posHistory),
        rootCpuct(rootCpuct),
        cpuct(cpuct),
        rootPolicyTemp(rootPolicyTemp),
        policyTemp(policyTemp),
        doReporting(doReporting),
        doUci(doUci),
        minimalUci(minimalUci) {}
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