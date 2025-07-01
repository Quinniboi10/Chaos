#pragma once

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>
#include <string>
#include <limits>
#include <atomic>
#include <array>
#include <bit>

#undef assert
#ifndef NDEBUG
    #include <boost/stacktrace.hpp>
    #undef assert
    #define assert(x) \
        if (!(x)) { \
            std::cout << std::endl << std::endl << boost::stacktrace::stacktrace() << std::endl << "Assertion failed: " << #x << ", file " << __FILE__ << ", line " << __LINE__ << std::endl; \
            std::terminate(); \
        }
#else
    #define assert(x) ;
#endif

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8  = uint8_t;

using i64 = int64_t;
using i32 = int32_t;
using i16 = int16_t;
using i8  = int8_t;

#ifdef _MSC_VER
    #include <__msvc_int128.hpp>
using u128 = std::_Unsigned128;
#else
using u128 = unsigned __int128;
#endif

using usize = size_t;

using std::popcount;
using std::string;
using std::atomic;
using std::vector;
using std::array;
using std::cerr;
using std::cout;
using std::endl;

enum Color : int {
    WHITE = 1,
    BLACK = 0,
};

enum GameState : i8 {
    ONGOING,
    LOSS,
    DRAW,
    WIN
};

constexpr array<std::string_view, 4> GAME_STATE_STR = { "ONGOING", "LOSS", "DRAW", "WIN" };

constexpr auto operator<=>(const GameState a, const GameState b) { return static_cast<int>(a) <=> static_cast<int>(b); }

//Inverts the color (WHITE -> BLACK) and (BLACK -> WHITE)
constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int {
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    NO_PIECE_TYPE
};

// clang-format off
enum Square : int {
    a1, b1, c1, d1, e1, f1, g1, h1,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a8, b8, c8, d8, e8, f8, g8, h8,
    NO_SQUARE
};

enum Direction : int {
    NORTH = 8,
    NORTH_EAST = 9,
    EAST = 1,
    SOUTH_EAST = -7,
    SOUTH = -8,
    SOUTH_WEST = -9,
    WEST = -1,
    NORTH_WEST = 7,
    NORTH_NORTH = 16,
    SOUTH_SOUTH = -16
};

enum File : int {
    AFILE, BFILE, CFILE, DFILE, EFILE, FFILE, GFILE, HFILE
};
enum Rank : int {
    RANK1, RANK2, RANK3, RANK4, RANK5, RANK6, RANK7, RANK8
};

constexpr u64 MASK_FILE[8] = {
  0x101010101010101, 0x202020202020202, 0x404040404040404, 0x808080808080808, 0x1010101010101010, 0x2020202020202020, 0x4040404040404040, 0x8080808080808080,
};
constexpr u64 MASK_RANK[8] = {
    0xff, 0xff00, 0xff0000, 0xff000000, 0xff00000000, 0xff0000000000, 0xff000000000000, 0xff00000000000000
};

inline Square& operator++(Square& s) { return s = Square(int(s) + 1); }
inline Square& operator--(Square& s) { return s = Square(int(s) - 1); }
constexpr Square operator+(Square s, Direction d) { return Square(int(s) + int(d)); }
constexpr Square operator-(Square s, Direction d) { return Square(int(s) - int(d)); }
inline Square& operator+=(Square& s, Direction d) { return s = s + d; }
inline Square& operator-=(Square& s, Direction d) { return s = s - d; }
//clang-format on

static inline const u16  Le               = 1;
static inline const bool IS_LITTLE_ENDIAN = *reinterpret_cast<const char*>(&Le) == 1;

enum MoveType {
    STANDARD_MOVE = 0, EN_PASSANT = 0x4000, CASTLE = 0x8000, PROMOTION = 0xC000
};

struct Colors {
    // ANSI codes for colors https://raw.githubusercontent.com/fidian/ansi/master/images/color-codes.png
    static constexpr std::string_view RESET = "\033[0m";

    // Basic colors
    static constexpr std::string_view BLACK = "\033[30m";
    static constexpr std::string_view RED = "\033[31m";
    static constexpr std::string_view GREEN = "\033[32m";
    static constexpr std::string_view YELLOW = "\033[33m";
    static constexpr std::string_view BLUE = "\033[34m";
    static constexpr std::string_view MAGENTA = "\033[35m";
    static constexpr std::string_view CYAN = "\033[36m";
    static constexpr std::string_view WHITE = "\033[37m";

    // Bright colors
    static constexpr std::string_view BRIGHT_BLACK = "\033[90m";
    static constexpr std::string_view BRIGHT_RED = "\033[91m";
    static constexpr std::string_view BRIGHT_GREEN = "\033[92m";
    static constexpr std::string_view BRIGHT_YELLOW = "\033[93m";
    static constexpr std::string_view BRIGHT_BLUE = "\033[94m";
    static constexpr std::string_view BRIGHT_MAGENTA = "\033[95m";
    static constexpr std::string_view BRIGHT_GYAN = "\033[96m";
    static constexpr std::string_view BRIGHT_WHITE = "\033[97m";

    static constexpr std::string_view GREY = BRIGHT_BLACK;
};

namespace internal {
    template <typename T, usize kN, usize... kNs>
    struct MultiArrayImpl {
        using Type = array<typename MultiArrayImpl<T, kNs...>::Type, kN>;
    };

    template <typename T, usize kN>
    struct MultiArrayImpl<T, kN> {
        using Type = array<T, kN>;
    };
}

template <typename T, usize... kNs>
using MultiArray = typename internal::MultiArrayImpl<T, kNs...>::Type;

struct Board;