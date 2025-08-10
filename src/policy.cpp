#include "policy.h"

#ifdef _MSC_VER
    #define MSVC
    #pragma push_macro("_MSC_VER")
    #undef _MSC_VER
#endif

#include "movegen.h"
#include "../external/incbin.h"

#ifdef MSVC
    #pragma pop_macro("_MSC_VER")
    #undef MSVC
#endif

#if !defined(_MSC_VER) || defined(__clang__)
INCBIN(POLICY, POLICYFILE);
#endif

#ifdef __AVX512F__
constexpr usize ALIGNMENT = 64;
#else
constexpr usize ALIGNMENT = 32;
#endif

struct PolicyAccumulator {
    alignas(ALIGNMENT) array<i16, HL_SIZE_P> underlying;

    explicit PolicyAccumulator(const Board& board);

    const i16& operator[](const usize& idx) const { return underlying[idx]; }
    i16&       operator[](const usize& idx) { return underlying[idx]; }
};

struct PolicyNN {
    alignas(ALIGNMENT) array<i8, HL_SIZE_P * 768> weightsToHL;
    alignas(ALIGNMENT) array<i8, HL_SIZE_P> hiddenLayerBias;
    alignas(ALIGNMENT) MultiArray<i8, 1880, HL_SIZE_P> weightsToOut;
    array<i8, 1880> outputBiases;

    static i16 ReLU(const i16 x);
    static i16 CReLU(const i16 x);
    static i32 SCReLU(const i16 x);

    static usize feature(const Color stm, const Color pieceColor, const PieceType piece, const Square square);
};

const PolicyNN nn = *reinterpret_cast<const PolicyNN*>(gPOLICYData);

PolicyAccumulator::PolicyAccumulator(const Board& board) {
    u64 whitePieces = board.pieces(WHITE);
    u64 blackPieces = board.pieces(BLACK);

    for (usize i = 0; i < underlying.size(); i++)
        underlying[i] = nn.hiddenLayerBias[i];

    while (whitePieces) {
        const Square sq = popLSB(whitePieces);

        const usize feature = PolicyNN::feature(board.stm, WHITE, board.getPiece(sq), sq);

        for (usize i = 0; i < HL_SIZE_P; i++)
            underlying[i] += nn.weightsToHL[feature * HL_SIZE_P + i];
    }

    while (blackPieces) {
        const Square sq = popLSB(blackPieces);

        const usize feature = PolicyNN::feature(board.stm, BLACK, board.getPiece(sq), sq);

        for (usize i = 0; i < HL_SIZE_P; i++)
            underlying[i] += nn.weightsToHL[feature * HL_SIZE_P + i];
    }

    for (i16& i : underlying) {
        if constexpr (ACTIVATION_P == ::ReLU)
            i = PolicyNN::ReLU(i);
        if constexpr (ACTIVATION_P == ::CReLU)
            i = PolicyNN::CReLU(i);
        if constexpr (ACTIVATION_P == ::SCReLU)
            i = PolicyNN::SCReLU(i);
    }
}

i16 PolicyNN::ReLU(const i16 x) {
    if (x < 0)
        return 0;
    return x;
}

i16 PolicyNN::CReLU(const i16 x) { return std::clamp<i16>(x, 0, Q_P); }

i32 PolicyNN::SCReLU(const i16 x) {
    if (x < 0)
        return 0;
    if (x > Q_P)
        return Q_P * Q_P;
    return x * x;
}

// Finds the input feature
usize PolicyNN::feature(const Color stm, const Color pieceColor, const PieceType piece, const Square square) {
    const bool enemy       = stm != pieceColor;
    const int  squareIndex = (stm == BLACK) ? flipRank(square) : static_cast<int>(square);

    return enemy * 64 * 6 + piece * 64 + squareIndex;
}

// Based on code from Vine
array<u64, 64>   ALL_DESTINATIONS;
array<usize, 65> OFFSETS;

void initPolicy() {
    // Destinations
    for (i32 sq = 0; sq < 64; sq++)
        ALL_DESTINATIONS[sq] = Movegen::getRookAttacks(static_cast<Square>(sq), 0) | Movegen::getBishopAttacks(static_cast<Square>(sq), 0) | Movegen::KNIGHT_ATTACKS[sq] | Movegen::KING_ATTACKS[sq];
    // Offsets
    usize curr = 0;
    for (i32 sq = 0; sq < 64; sq++) {
        OFFSETS[sq] = curr;
        curr += static_cast<usize>(popcount(ALL_DESTINATIONS[sq]));
    }
    OFFSETS[64] = curr;
}


usize moveIdx(const Color stm, const Move m) {
    const i32 flipper = stm == Color::BLACK ? 56 : 0;
    if (m.typeOf() == PROMOTION) {
        constexpr usize PROMO_STRIDE = 22;
        const i32       promoId      = 2 * fileOf(m.from()) + fileOf(m.to());
        const i32       kind         = m.promo() - 1;
        return OFFSETS[64] + kind * PROMO_STRIDE + promoId;
    }

    const Square from  = Square(m.from() ^ flipper);
    const Square to    = Square(m.to() ^ flipper);
    const u64    all   = ALL_DESTINATIONS[from];
    const u64    below = to == 0 ? 0 : all & ((1ULL << to) - 1);
    return OFFSETS[from] + static_cast<usize>(popcount(below));
}

float policyScore(const Color stm, const PolicyAccumulator& policyAccumulator, const Move m) {
    const usize idx  = moveIdx(stm, m);
    i32         eval = nn.outputBiases[idx];
    for (usize i = 0; i < HL_SIZE_P; i++)
        eval += policyAccumulator[i] * nn.weightsToOut[idx][i];

    return static_cast<float>(eval) / (Q_P * Q_P);
}

void fillPolicy(const Board& board, Tree& tree, const Node& parent, const float temperature) {
    const PolicyAccumulator accum(board);

    float maxScore = -std::numeric_limits<float>::infinity();
    float sum      = 0;

    const u8    half        = parent.firstChild.load().half();
    const usize firstIdx    = parent.firstChild.load().index();
    const u8    numChildren = parent.numChildren.load();

    vector<float> scores;
    scores.reserve(parent.numChildren);

    // Get raw scores and find max
    for (usize idx = firstIdx; idx < firstIdx + numChildren; idx++) {
        Node&       child = tree[{ idx, half }];
        const float score = policyScore(board.stm, accum, child.move);
        scores.push_back(score);
        maxScore = std::max(score, maxScore);
    }

    // Exponentiate and sum
    for (usize idx = 0; idx < numChildren; idx++) {
        scores[idx] = std::exp((scores[idx] - maxScore) / temperature);
        sum += scores[idx];
    }

    // Normalize
    for (usize idx = 0; idx < numChildren; idx++) {
        atomic<float>& score = tree[{ idx + firstIdx, half }].policy;
        const float    exp   = scores[idx];
        score.store(exp / sum);
    }
}