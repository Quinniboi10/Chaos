#include "policy.h"

#ifdef _MSC_VER
    #define MSVC
    #pragma push_macro("_MSC_VER")
    #undef _MSC_VER
#endif

#include "movegen.h"
#include "simd.h"
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
    using namespace simd;
    static_assert(HL_SIZE_P % VECTOR_SIZE<i16> == 0, "Policy HL size is not compatible with the size of this CPU's native register");

    const usize idx = moveIdx(stm, m);

    Vector<i32> outputAccumulator{};

    for (usize i = 0; i < HL_SIZE_P; i += VECTOR_SIZE<i16>) {
        // Load values
        const Vector<i16> accumValues = load_ep<i16>(&policyAccumulator[i]);

        // Load weights
        const Vector<i16> weights = load_ep<i8, i16>(&nn.weightsToOut[idx][i]);

        // Apply weights
        const Vector<i32> weighted = madd_epi16(accumValues, weights);

        outputAccumulator = add_ep<i32>(outputAccumulator, weighted);
    }

    return static_cast<float>(reduce_ep<i32>(outputAccumulator) + nn.outputBiases[idx]) / (Q_P * Q_P);
}

void fillPolicy(const Board& board, Tree& tree, Node& parent, const float temperature) {
    const PolicyAccumulator accum(board);

    float maxScore = -std::numeric_limits<float>::infinity();
    float sum      = 0;

    vector<float> scores;
    scores.reserve(parent.numChildren);

    Node* firstChild = &tree[parent.firstChild.load()];
    const Node* end = firstChild + parent.numChildren.load();

    // Get raw scores and find max
    for (const Node* node = firstChild; node != end; node++) {
        const float score = policyScore(board.stm, accum, node->move);
        scores.push_back(score);
        maxScore = std::max(score, maxScore);
    }

    // Exponentiate and sum
    const float tempMult = 1 / temperature;
    for (float& score : scores) {
        score = std::exp((score - maxScore) * tempMult);
        sum += score;
    }

    float sumOfSquares = 0;

    // Normalize
    const float sumMult = 1 / sum;
    for (usize idx = 0; idx < scores.size(); idx++) {
        const float score = scores[idx] * sumMult;
        (firstChild + idx)->setPolicy(score);

        sumOfSquares += score * score;
    }

    parent.setGiniImpurity(std::clamp<float>(1 - sumOfSquares, 0, 1));
}