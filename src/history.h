#pragma once

#include "util.h"
#include "move.h"
#include "types.h"
#include "tunable.h"

struct ButterflyHistory {
    // Indexed [stm][from][to]
    MultiArray<RelaxedAtomic<i32>, 2, 64, 64> butterfly{};

    static i32 scaleBonus(const i32 score, const i32 bonus) { return bonus - score * std::abs(bonus) / BUTTERFLY_BONUS_DIVISOR; }

    i32 getEntry(const Color stm, const Move m) const { return butterfly[stm][m.from()][m.to()].load(); }

    void update(const Color stm, const Move m, float wdl) {
        assert(std::isfinite(wdl));

        wdl             = std::clamp<float>(wdl, -0.9999, 0.9999);
        const i32 cp    = wdlToCP(wdl);
        auto&     entry = butterfly[stm][m.from()][m.to()];

        entry.getUnderlying() += scaleBonus(entry.load(), cp);
    }
};