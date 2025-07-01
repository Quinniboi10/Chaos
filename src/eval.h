#pragma once

#include "board.h"
#include "tunable.h"

static i32 materialEval(const Board& board) {
    return ((popcount(board.pieces(WHITE, PAWN)) - popcount(board.pieces(BLACK, PAWN))) * PIECE_VALUES[PAWN]
         + (popcount(board.pieces(WHITE, KNIGHT)) - popcount(board.pieces(BLACK, KNIGHT))) * PIECE_VALUES[KNIGHT]
         + (popcount(board.pieces(WHITE, BISHOP)) - popcount(board.pieces(BLACK, BISHOP))) * PIECE_VALUES[BISHOP]
         + (popcount(board.pieces(WHITE, ROOK)) - popcount(board.pieces(BLACK, ROOK))) * PIECE_VALUES[ROOK]
         + (popcount(board.pieces(WHITE, QUEEN)) - popcount(board.pieces(BLACK, QUEEN))) * PIECE_VALUES[QUEEN])
         * (1 - 2 * (board.stm == BLACK));
};