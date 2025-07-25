#pragma once

#include "types.h"
#include "move.h"
#include "board.h"
#include "stopwatch.h"

enum MovegenMode {
    ALL_MOVES,
    NOISY_ONLY
};

namespace Movegen {
// Tables from https://github.com/Disservin/chess-library/blob/cf3bd56474168605201a01eb78b3222b8f9e65e4/include/chess.hpp#L780
constexpr u64 KNIGHT_ATTACKS[64] = {0x0000000000020400, 0x0000000000050800, 0x00000000000A1100, 0x0000000000142200, 0x0000000000284400, 0x0000000000508800, 0x0000000000A01000, 0x0000000000402000,
                                    0x0000000002040004, 0x0000000005080008, 0x000000000A110011, 0x0000000014220022, 0x0000000028440044, 0x0000000050880088, 0x00000000A0100010, 0x0000000040200020,
                                    0x0000000204000402, 0x0000000508000805, 0x0000000A1100110A, 0x0000001422002214, 0x0000002844004428, 0x0000005088008850, 0x000000A0100010A0, 0x0000004020002040,
                                    0x0000020400040200, 0x0000050800080500, 0x00000A1100110A00, 0x0000142200221400, 0x0000284400442800, 0x0000508800885000, 0x0000A0100010A000, 0x0000402000204000,
                                    0x0002040004020000, 0x0005080008050000, 0x000A1100110A0000, 0x0014220022140000, 0x0028440044280000, 0x0050880088500000, 0x00A0100010A00000, 0x0040200020400000,
                                    0x0204000402000000, 0x0508000805000000, 0x0A1100110A000000, 0x1422002214000000, 0x2844004428000000, 0x5088008850000000, 0xA0100010A0000000, 0x4020002040000000,
                                    0x0400040200000000, 0x0800080500000000, 0x1100110A00000000, 0x2200221400000000, 0x4400442800000000, 0x8800885000000000, 0x100010A000000000, 0x2000204000000000,
                                    0x0004020000000000, 0x0008050000000000, 0x00110A0000000000, 0x0022140000000000, 0x0044280000000000, 0x0088500000000000, 0x0010A00000000000, 0x0020400000000000};

constexpr u64 KING_ATTACKS[64] = {0x0000000000000302, 0x0000000000000705, 0x0000000000000E0A, 0x0000000000001C14, 0x0000000000003828, 0x0000000000007050, 0x000000000000E0A0, 0x000000000000C040,
                                  0x0000000000030203, 0x0000000000070507, 0x00000000000E0A0E, 0x00000000001C141C, 0x0000000000382838, 0x0000000000705070, 0x0000000000E0A0E0, 0x0000000000C040C0,
                                  0x0000000003020300, 0x0000000007050700, 0x000000000E0A0E00, 0x000000001C141C00, 0x0000000038283800, 0x0000000070507000, 0x00000000E0A0E000, 0x00000000C040C000,
                                  0x0000000302030000, 0x0000000705070000, 0x0000000E0A0E0000, 0x0000001C141C0000, 0x0000003828380000, 0x0000007050700000, 0x000000E0A0E00000, 0x000000C040C00000,
                                  0x0000030203000000, 0x0000070507000000, 0x00000E0A0E000000, 0x00001C141C000000, 0x0000382838000000, 0x0000705070000000, 0x0000E0A0E0000000, 0x0000C040C0000000,
                                  0x0003020300000000, 0x0007050700000000, 0x000E0A0E00000000, 0x001C141C00000000, 0x0038283800000000, 0x0070507000000000, 0x00E0A0E000000000, 0x00C040C000000000,
                                  0x0302030000000000, 0x0705070000000000, 0x0E0A0E0000000000, 0x1C141C0000000000, 0x3828380000000000, 0x7050700000000000, 0xE0A0E00000000000, 0xC040C00000000000,
                                  0x0203000000000000, 0x0507000000000000, 0x0A0E000000000000, 0x141C000000000000, 0x2838000000000000, 0x5070000000000000, 0xA0E0000000000000, 0x40C0000000000000};

u64 pawnAttackBB(Color c, int sq);

template<MovegenMode mode>
void pawnMoves(const Board& board, MoveList& moves);
template<MovegenMode mode>
void knightMoves(const Board& board, MoveList& moves);
template<MovegenMode mode>
void bishopMoves(const Board& board, MoveList& moves);
template<MovegenMode mode>
void rookMoves(const Board& board, MoveList& moves);
template<MovegenMode mode>
void kingMoves(const Board& board, MoveList& moves);
void initializeAllDatabases();

MoveList generateMoves(const Board& board);

void perft(Board& board, usize depth, bool bulk);
void perftSuite(const string filePath);

u64 getBishopAttacks(Square square, u64 occ);
u64 getXrayBishopAttacks(Square square, u64 occ, u64 blockers);
u64 getRookAttacks(Square square, u64 occ);
u64 getXrayRookAttacks(Square square, u64 occ, u64 blockers);

u64 pawnAttacks(Color c, const Board& board);
u64 knightAttacks(Color c, const Board& board);
u64 bishopAttacks(Color c, const Board& board);
u64 rookAttacks(Color c, const Board& board);
u64 kingAttacks(Color c, const Board& board);
u64 getAttacks(Color c, const Board& board);

void pawnMoves(const Board& board, MoveList& moves);
void knightMoves(const Board& board, MoveList& moves);
void bishopMoves(const Board& board, MoveList& moves);
void rookMoves(const Board& board, MoveList& moves);
void kingMoves(const Board& board, MoveList& moves);
}