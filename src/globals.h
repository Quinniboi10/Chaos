#pragma once

#include "types.h"

extern bool  chess960;
extern bool  inDatagen;
extern usize multiPV;
extern usize threadCount;

extern MultiArray<u64, 64, 64> LINE;
extern MultiArray<u64, 64, 64> LINESEG;