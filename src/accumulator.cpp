#include "accumulator.h"
#include "nn.h"
#include "board.h"

void AccumulatorPair::load(const Board& board) {
    u64 whitePieces = board.pieces(WHITE);
    u64 blackPieces = board.pieces(BLACK);

    white = valueNetwork.hiddenLayerBias;
    black = valueNetwork.hiddenLayerBias;

    while (whitePieces) {
        const Square sq = popLSB(whitePieces);

        const usize whiteInputFeature = ValueNetwork::feature(WHITE, WHITE, board.getPiece(sq), sq);
        const usize blackInputFeature = ValueNetwork::feature(BLACK, WHITE, board.getPiece(sq), sq);

        for (usize i = 0; i < HL_SIZE; i++) {
            white[i] += valueNetwork.weightsToHL[whiteInputFeature * HL_SIZE + i];
            black[i] += valueNetwork.weightsToHL[blackInputFeature * HL_SIZE + i];
        }
    }

    while (blackPieces) {
        const Square sq = popLSB(blackPieces);

        const usize whiteInputFeature = ValueNetwork::feature(WHITE, BLACK, board.getPiece(sq), sq);
        const usize blackInputFeature = ValueNetwork::feature(BLACK, BLACK, board.getPiece(sq), sq);

        for (usize i = 0; i < HL_SIZE; i++) {
            white[i] += valueNetwork.weightsToHL[whiteInputFeature * HL_SIZE + i];
            black[i] += valueNetwork.weightsToHL[blackInputFeature * HL_SIZE + i];
        }
    }
}