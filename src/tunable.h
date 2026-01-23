#pragma once

#include "types.h"

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

Tunable(CPUCT, 12439);
Tunable(ROOT_CPUCT, 24473);

Tunable(POLICY_TEMPERATURE, 7120);
Tunable(ROOT_POLICY_TEMPERATURE, 27294);

Tunable(EG_POLICY_TEMPERATURE, 6319);
Tunable(EG_ROOT_POLICY_TEMPERATURE, 24855);

Tunable(EVAL_DIVISOR, 411);
Tunable(CPUCT_VISIT_SCALE, 8172);

Tunable(FPU_SHARPNESS_MARGIN, 500);

Tunable(POLICY_MATERIAL_PHASE_DIVISOR, 4356);

Tunable(BUTTERFLY_BONUS_DIVISOR, 8763);
Tunable(BUTTERFLY_POLICY_DIVISOR, 14035);

Tunable(GINI_BASE, 4694);
Tunable(GINI_SCALAR, 17662);
Tunable(GINI_MIN, 8838);
Tunable(GINI_MAX, 15738);

Tunable(PAWN_VALUE, 100);
Tunable(KNIGHT_VALUE, 310);
Tunable(BISHOP_VALUE, 345);
Tunable(ROOK_VALUE, 516);
Tunable(QUEEN_VALUE, 917);  