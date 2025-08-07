#pragma once

#include "search.h"


#include <bit>
#include <numbers>
#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <string_view>

#include "types.h"
#include "../external/fmt/fmt/color.h"
#include "../external/fmt/fmt/format.h"

#define ctzll(x) std::countr_zero(x)

inline bool readBit(u64 bb, int sq) { return (1ULL << sq) & bb; }

template<bool value>
inline void setBit(auto& bitboard, usize index) {
    assert(index <= sizeof(bitboard) * 8);
    if constexpr (value)
        bitboard |= (1ULL << index);
    else
        bitboard &= ~(1ULL << index);
}

inline Square getLSB(auto bb) {
    assert(bb > 0);
    return static_cast<Square>(ctzll(bb));
}

inline Square popLSB(auto& bb) {
    assert(bb > 0);
    Square sq = getLSB(bb);
    bb &= bb - 1;
    return sq;
}

template<int dir>
inline u64 shift(u64 bb) {
    return dir > 0 ? bb << dir : bb >> -dir;
}

inline u64 shift(int dir, u64 bb) { return dir > 0 ? bb << dir : bb >> -dir; }

inline double sigmoid(double x) { return 2 / (1 + std::pow(std::numbers::e, -x)) - 1; }
inline double inverseSigmoid(double x) { return std::log((1 + x) / (1 - x)); }

inline double cpToWDL(int cp) { return sigmoid((static_cast<double>(cp) / EVAL_DIVISOR)); }
inline i32 wdlToCP(double wdl) {
    assert(wdl > -1);
    assert(wdl < 1);
    return inverseSigmoid(wdl) * EVAL_DIVISOR;
}

inline std::vector<string> split(const string& str, char delim) {
    std::vector<std::string> result;

    std::istringstream stream(str);

    for (std::string token{}; std::getline(stream, token, delim);) {
        if (token.empty())
            continue;

        result.push_back(token);
    }

    return result;
}

// Function from stockfish
template<typename IntType>
inline IntType readLittleEndian(std::istream& stream) {
    IntType result;

    if (IS_LITTLE_ENDIAN)
        stream.read(reinterpret_cast<char*>(&result), sizeof(IntType));
    else {
        std::uint8_t                  u[sizeof(IntType)];
        std::make_unsigned_t<IntType> v = 0;

        stream.read(reinterpret_cast<char*>(u), sizeof(IntType));
        for (usize i = 0; i < sizeof(IntType); ++i)
            v = (v << 8) | u[sizeof(IntType) - i - 1];

        std::memcpy(&result, &v, sizeof(IntType));
    }

    return result;
}

template<typename T, typename U>
inline void deepFill(T& dest, const U& val) {
    dest = val;
}

template<typename T, usize N, typename U>
inline void deepFill(std::array<T, N>& arr, const U& value) {
    for (auto& element : arr) {
        deepFill(element, value);
    }
}

constexpr Rank rankOf(Square s) { return Rank(s >> 3); }
constexpr File fileOf(Square s) { return File(s & 0b111); }

constexpr Square flipRank(Square s) { return Square(s ^ 0b111000); }

constexpr Square toSquare(Rank rank, File file) { return static_cast<Square>((static_cast<int>(rank) << 3) | file); }

// Takes square (h8) and converts it into a bitboard index (64)
constexpr Square parseSquare(const std::string_view square) { return static_cast<Square>((square.at(1) - '1') * 8 + (square.at(0) - 'a')); }

// Takes a square (64) and converts into algebraic notation (h8)
inline string squareToAlgebraic(int sq) { return fmt::format("{}{}", static_cast<char>('a' + (sq % 8)), static_cast<char>('1' + (sq / 8))); }

constexpr u8 castleIndex(Color c, bool kingside) { return c == WHITE ? (kingside ? 3 : 2) : (kingside ? 1 : 0); }

// Print a bitboard (for debugging individual bitboards)
inline void printBitboard(u64 bitboard) {
    for (int rank = 7; rank >= 0; --rank) {
        cout << "+---+---+---+---+---+---+---+---+" << endl;
        for (int file = 0; file < 8; ++file) {
            int  i            = rank * 8 + file;  // Map rank and file to bitboard index
            char currentPiece = readBit(bitboard, i) ? '1' : ' ';

            cout << "| " << currentPiece << " ";
        }
        cout << "|" << endl;
    }
    cout << "+---+---+---+---+---+---+---+---+" << endl;
}

// Formats a number with commas
inline string formatNum(i64 v) {
    auto s = std::to_string(v);

    int n = s.length() - 3;
    if (v < 0)
        n--;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }

    return s;
}

// Fancy formats a time
inline string formatTime(u64 timeInMS) {
    long long seconds = timeInMS / 1000;
    long long hours   = seconds / 3600;
    seconds %= 3600;
    long long minutes = seconds / 60;
    seconds %= 60;

    string result;

    if (hours > 0)
        result += std::to_string(hours) + "h ";
    if (minutes > 0 || hours > 0)
        result += std::to_string(minutes) + "m ";
    if (seconds > 0 || minutes > 0 || hours > 0)
        result += std::to_string(seconds) + "s";
    if (result == "")
        return std::to_string(timeInMS) + "ms";
    return result;
}

inline string padStr(string str, i64 target, u64 minPadding = 2) {
    i64 padding = std::max<i64>(target - static_cast<i64>(str.length()), minPadding);
    for (i64 i = 0; i < padding; i++)
        str += " ";
    return str;
}

// Score color
inline void printColoredScore(double wdl) {
    double colorWdl = std::clamp(wdl * 5, -1.0, 1.0);
    int r, g, b;

    if (colorWdl < 0) {
        r = static_cast<int>(200 - colorWdl * (240 - 200));
        g = static_cast<int>(60 - colorWdl * (240 - 60));
        b = static_cast<int>(60 - colorWdl * (240 - 60));
    }
    else {
        r = static_cast<int>(240 + colorWdl * (60 - 240));
        g = static_cast<int>(240 + colorWdl * (180 - 240));
        b = static_cast<int>(240 + colorWdl * (60 - 240));
    }

    fmt::print(fmt::fg(fmt::rgb(r, g, b)), "{:.2f}", wdlToCP(wdl) / 100.0f);
}

// Heat color
inline void heatColor(float t, const string& text) {
    t = std::clamp(t, 0.0f, 1.0f);
    int r, g, b = 0;
    if (t < 0.5f) {
        const float ratio = t / 0.5f;
        r = 255;
        g = static_cast<int>(ratio * 255);
    } else {
        const float ratio = (t - 0.5f) / 0.5f;
        r = static_cast<int>(255 * (1.0f - ratio));
        g = 255;
    }

    fmt::print(fg(fmt::rgb(r, g, b)), "{}", text);
}

// Colored progress bar
inline void coloredProgBar(const usize length, const float fill) {
    if (length == 0) {
        fmt::print("[] 0%\n");
        return;
    }
    fmt::print("[");
    for (usize i = 0; i < length; ++i) {
        float percentage = static_cast<float>(i) / (length - 1);
        if (percentage <= fill) {
            heatColor(1 - percentage, "#");
        } else {
            fmt::print(".");
        }
    }
    fmt::print("] {}%\n", static_cast<usize>(fill * 100));
}

// Colorless progress bar
inline void progressBar(const usize length, const float fill) {
    if (length == 0) {
        cout << "[] 0%";
        return;
    }
    cout << "[";
    for (usize i = 0; i < length; ++i)
        cout << (static_cast<float>(i) / (length - 1) <= fill ? "#" : ".");
    cout << "] " << static_cast<usize>(fill * 100) << "%";
}

inline int findIndexOf(const auto arr, string entry) {
    auto it = std::find(arr.begin(), arr.end(), entry);
    if (it != arr.end()) {
        return std::distance(arr.begin(), it);
    }
    return -1;
}

namespace cursor {
    static void clearAll() { cout << "\033[2J\033[H"; }
    static void clear() { cout << "\033[2K\r"; }
    static void home() { cout << "\033[H"; }
    static void up() { cout << "\033[A"; }
    static void down() { cout << "\033[B"; }
    static void begin() { cout << "\033[1G"; }
    static void goTo(const usize x, const usize y) { cout << "\033[" << y << ";" << x << "H"; }

    static void hide() { cout << "\033[?25l"; }
    static void show() { cout << "\033[?25h"; }
}