#pragma once

#include <algorithm>

#include "types.h"

struct Board;

class Move {
    u16 move;

   public:
    constexpr Move()  = default;
    constexpr ~Move() = default;

    constexpr Move(u8 startSquare, u8 endSquare, MoveType flags = STANDARD_MOVE) {
        move = startSquare | flags;
        move |= endSquare << 6;
    }

    constexpr Move(u8 startSquare, u8 endSquare, PieceType promo) {
        move = startSquare | PROMOTION;
        move |= endSquare << 6;
        move |= (promo - 1) << 12;
    }

    Move(string strIn, Board& board);

    constexpr static Move null() { return Move(a1, a1); }


    string toString() const;

    Square from() const { return Square(move & 0b111111); }
    Square to() const { return Square((move >> 6) & 0b111111); }

    MoveType typeOf() const { return MoveType(move & 0xC000); }

    PieceType promo() const {
        assert(typeOf() == PROMOTION);
        return PieceType(((move >> 12) & 0b11) + 1);
    }

    bool isNull() const { return *this == null(); }

    bool operator==(const Move other) const { return move == other.move; }

    friend std::ostream& operator<<(std::ostream& os, const Move& m) {
        os << m.toString();
        return os;
    }
};

struct MoveList {
    array<Move, 256> moves;
    usize            length;

    constexpr MoveList() { length = 0; }

    void add(Move m) {
        assert(length < 256);
        moves[length++] = m;
    }

    void add(u8 from, u8 to, MoveType flags = STANDARD_MOVE) { add(Move(from, to, flags)); }
    void add(u8 from, u8 to, PieceType promo) { add(Move(from, to, promo)); }

    auto begin() { return moves.begin(); }
    auto end() { return moves.begin() + length; }
    auto begin() const { return moves.begin(); }
    auto end() const { return moves.begin() + length; }

    bool has(Move m) const { return std::find(begin(), end(), m) != end(); }
    void remove(Move m) {
        assert(has(m));
        auto location = std::find(begin(), end(), m);
        if (location != end())
            *(location) = moves[--length];
    }

    Move& operator[](usize idx) { return moves[idx]; }
    const Move& operator[](usize idx) const { return moves[idx]; }
};