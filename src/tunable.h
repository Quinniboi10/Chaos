#pragma once

#include "types.h"

#define TUNE

#ifdef TUNE
struct IndividualOption {
    string name;
    i32    value;
    i32    min;
    i32    max;
    i32    step;

    IndividualOption(const string& name, i32 value);

    void setValue(const i32 value) {
        this->value = value;
    }

    operator i32() const {
        return value;
    }
};

inline std::vector<IndividualOption*> tunables;

inline IndividualOption::IndividualOption(const string& name, const i32 value) {
    this->name  = name;
    this->value = value;
    this->min   = value / 2;
    this->max   = value * 2;
    this->step  = (max - min) / 20;

    tunables.push_back(this);
}

static void setTunable(const string& name, const i32 value) {
    for (const auto& tunable : tunables) {
        if (tunable->name == name) {
            tunable->value = value;
            break;
        }
    }
}

static void printTuneUCI() {
    for (const auto& tunable : tunables)
        cout << "option name " << tunable->name << " type spin default " << tunable->value << " min " << tunable->min << " max " << tunable->max << endl;
}

static void printTuneOB() {
    for (const auto& tunable : tunables)
        cout << tunable->name << ", int, " << tunable->value << ", " << tunable->min << ", " << tunable->max << ", " << tunable->step << ", " << 0.002 / (std::min<double>(0.5, tunable->step) / 0.5) << endl;
}

#define Tunable(name, value) \
inline IndividualOption name { \
#name, value \
}
#else
#define Tunable(name, value) constexpr i32 name = value
#endif

// Floating points are quantized by 10'000
// 1.0 -> 10'000

Tunable(CPUCT, 11000);
Tunable(ROOT_CPUCT, 24000);

Tunable(POLICY_TEMPERATURE, 7500);
Tunable(ROOT_POLICY_TEMPERATURE, 29000);

Tunable(EG_POLICY_TEMPERATURE, 6000);
Tunable(EG_ROOT_POLICY_TEMPERATURE, 25000);

Tunable(EVAL_DIVISOR, 400);
Tunable(CPUCT_VISIT_SCALE, 8192);

Tunable(POLICY_MATERIAL_PHASE_DIVISOR, 4500);

Tunable(BUTTERFLY_BONUS_DIVISOR, 8192);
Tunable(BUTTERFLY_POLICY_DIVISOR, 16384);

Tunable(GINI_BASE, 4500);
Tunable(GINI_SCALAR, 16000);
Tunable(GINI_MIN, 10000);
Tunable(GINI_MAX, 15000);

Tunable(PAWN_VALUE, 100);
Tunable(KNIGHT_VALUE, 316);
Tunable(BISHOP_VALUE, 328);
Tunable(ROOK_VALUE, 493);
Tunable(QUEEN_VALUE, 982);