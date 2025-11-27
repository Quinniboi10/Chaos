#pragma once

#include "util.h"
#include "move.h"
#include "types.h"
#include "tunable.h"

struct ButterflyHistory {
    // Indexed [stm][from][to][to piece type]
    MultiArray<RelaxedAtomic<i32>, 2, 64, 64, 7> butterfly{};

    static i32 scaleBonus(const i32 score, const i32 bonus) {
        return bonus - score * std::abs(bonus) / BUTTERFLY_BONUS_DIVISOR;
    }

    i32 getEntry(const Board board, const Move m) const {
        const Square from = m.from();
        const Square to = m.to();
        return butterfly[board.stm][from][to][board.getPiece(to)].load();
    }

    void update(const Board board, const Move m, float wdl) {
        assert(std::isfinite(wdl));

        const Square from = m.from();
        const Square to = m.to();

        wdl = std::clamp<float>(wdl, -0.9999, 0.9999);
        const i32 cp = wdlToCP(wdl);
        auto& entry = butterfly[board.stm][from][to][board.getPiece(to)];

        entry.getUnderlying() += scaleBonus(entry.load(), cp);
    }
};