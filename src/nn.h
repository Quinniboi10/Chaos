#pragma once

#include "types.h"
#include "accumulator.h"

namespace NN {
    i16 ReLU(const i16 x);
    i16 CReLU(const i16 x);
    i32 SCReLU(const i16 x);
}

struct ValueNetwork {
    alignas(ALIGNMENT) array<i16, HL_SIZE * 768> weightsToHL;
    alignas(ALIGNMENT) array<i16, HL_SIZE> hiddenLayerBias;
    alignas(ALIGNMENT) MultiArray<i16, OUTPUT_BUCKETS, HL_SIZE * 2> weightsToOut;
    array<i16, OUTPUT_BUCKETS> outputBias;

    i32 vectorizedSCReLU(const Accumulator& stm, const Accumulator& nstm, usize bucket);

    static usize feature(Color perspective, Color color, PieceType piece, Square square);

    void loadNetwork(const string& filepath);

    int  forwardPass(const Board* board, const AccumulatorPair& accumulators);
    void showBuckets(const Board* board, const AccumulatorPair& accumulators);

    double evaluate(const Board& board);
};

extern ValueNetwork valueNetwork;