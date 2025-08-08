#include "eval.h"

// Embed of NNs
#ifdef _MSC_VER
    #define MSVC
    #pragma push_macro("_MSC_VER")
    #undef _MSC_VER
#endif

#include "../external/incbin.h"

#ifdef MSVC
    #pragma pop_macro("_MSC_VER")
    #undef MSVC
#endif

#if !defined(_MSC_VER) || defined(__clang__)
INCBIN(EVAL, VALUEFILE);
#endif

#ifdef __AVX512F__
constexpr usize ALIGNMENT = 64;
#else
constexpr usize ALIGNMENT = 32;
#endif

struct Accumulator {
    alignas(ALIGNMENT) array<i16, HL_SIZE> underlying;

    explicit Accumulator(const Board& board);

    const i16& operator[](const usize& idx) const { return underlying[idx]; }
    i16& operator[](const usize& idx) { return underlying[idx]; }
};

struct NN {
    alignas(ALIGNMENT) array<i16, HL_SIZE * 768> weightsToHL;
    alignas(ALIGNMENT) array<i16, HL_SIZE> hiddenLayerBias;
    alignas(ALIGNMENT) array<i16, HL_SIZE> weightsToOut;
    i16 outputBias;

    static i16 ReLU(const i16 x);
    static i16 CReLU(const i16 x);

    i32 vectorizedSCReLU(const Accumulator& accum) const;

    static usize feature(const Color stm, const Color pieceColor, const PieceType piece, const Square square);
};

const NN nn = *reinterpret_cast<const NN*>(gEVALData);

 Accumulator::Accumulator(const Board& board) {
     u64 whitePieces = board.pieces(WHITE);
     u64 blackPieces = board.pieces(BLACK);

     underlying = nn.hiddenLayerBias;

     while (whitePieces) {
         const Square sq = popLSB(whitePieces);

         const usize feature = NN::feature(board.stm, WHITE, board.getPiece(sq), sq);

         for (usize i = 0; i < HL_SIZE; i++)
             underlying[i] += nn.weightsToHL[feature * HL_SIZE + i];
     }

     while (blackPieces) {
         const Square sq = popLSB(blackPieces);

         const usize feature = NN::feature(board.stm, BLACK, board.getPiece(sq), sq);

         for (usize i = 0; i < HL_SIZE; i++)
             underlying[i] += nn.weightsToHL[feature * HL_SIZE + i];
     }
 }

i16 NN::ReLU(const i16 x) {
    if (x < 0)
        return 0;
    return x;
}

i16 NN::CReLU(const i16 x) {
    if (x < 0)
        return 0;
    if (x > QA)
        return QA;
    return x;
}

#if defined(__x86_64__) || defined(__amd64__) || (defined(_WIN64) && (defined(_M_X64) || defined(_M_AMD64)) || defined(__ARM_NEON))
    #ifndef __ARM_NEON
        #include <immintrin.h>
    #endif
    #if defined(__AVX512F__)
        #pragma message("Using AVX512 NNUE inference")
using Vectori16 = __m512i;
using Vectori32 = __m512i;
        #define set1_epi16 _mm512_set1_epi16
        #define load_epi16(x) _mm512_load_si512(reinterpret_cast<const Vectori16*>(x))
        #define min_epi16 _mm512_min_epi16
        #define max_epi16 _mm512_max_epi16
        #define madd_epi16 _mm512_madd_epi16
        #define mullo_epi16 _mm512_mullo_epi16
        #define add_epi32 _mm512_add_epi32
        #define reduce_epi32 _mm512_reduce_add_epi32
    #elif defined(__AVX2__)
        #pragma message("Using AVX2 NNUE inference")
using Vectori16 = __m256i;
using Vectori32 = __m256i;
        #define set1_epi16 _mm256_set1_epi16
        #define load_epi16(x) _mm256_load_si256(reinterpret_cast<const Vectori16*>(x))
        #define min_epi16 _mm256_min_epi16
        #define max_epi16 _mm256_max_epi16
        #define madd_epi16 _mm256_madd_epi16
        #define mullo_epi16 _mm256_mullo_epi16
        #define add_epi32 _mm256_add_epi32
        #define reduce_epi32 \
            [](Vectori32 vec) { \
                __m128i xmm1 = _mm256_extracti128_si256(vec, 1); \
                __m128i xmm0 = _mm256_castsi256_si128(vec); \
                xmm0         = _mm_add_epi32(xmm0, xmm1); \
                xmm1         = _mm_shuffle_epi32(xmm0, 238); \
                xmm0         = _mm_add_epi32(xmm0, xmm1); \
                xmm1         = _mm_shuffle_epi32(xmm0, 85); \
                xmm0         = _mm_add_epi32(xmm0, xmm1); \
                return _mm_cvtsi128_si32(xmm0); \
            }
    #elif defined(__ARM_NEON)
        #include <arm_neon.h>
        #pragma message("Using NEON NNUE inference")
using Vectori16 = int16x8_t;
using Vectori32 = int32x4_t;
        #define set1_epi16 vdupq_n_s16
        #define load_epi16(x) vld1q_s16(reinterpret_cast<const i16*>(x))
        #define min_epi16 vminq_s16
        #define max_epi16 vmaxq_s16
        #define madd_epi16 \
            [](Vectori16 a, Vectori16 b) { \
                const Vectori16 low = vmull_s16(vget_low_s16(a), vget_low_s16(b)); \
                const Vectori16 high = vmull_high_s16(a, b); \
                return vpaddq_s32(low, high); \
            }
        #define mullo_epi16 vmulq_s16
        #define add_epi32 vaddq_s32
        #define reduce_epi32 vaddvq_s32
    #else
        #pragma message("Using SSE NNUE inference")
// Assumes SSE support here
using Vectori16 = __m128i;
using Vectori32 = __m128i;
        #define set1_epi16 _mm_set1_epi16
        #define load_epi16(x) _mm_load_si128(reinterpret_cast<const Vectori16*>(x))
        #define min_epi16 _mm_min_epi16
        #define max_epi16 _mm_max_epi16
        #define madd_epi16 _mm_madd_epi16
        #define mullo_epi16 _mm_mullo_epi16
        #define add_epi32 _mm_add_epi32
        #define reduce_epi32 \
            [](Vectori32 vec) { \
                __m128i xmm1 = _mm_shuffle_epi32(vec, 238); \
                vec          = _mm_add_epi32(vec, xmm1); \
                xmm1         = _mm_shuffle_epi32(vec, 85); \
                vec          = _mm_add_epi32(vec, xmm1); \
                return _mm_cvtsi128_si32(vec); \
            }
    #endif
i32 NN::vectorizedSCReLU(const Accumulator& accum) const {
    constexpr usize VECTOR_SIZE = sizeof(Vectori16) / sizeof(i16);
    static_assert(HL_SIZE % VECTOR_SIZE == 0, "HL size must be divisible by the native register size of your CPU for vectorization to work");
    const Vectori16 VEC_QA   = set1_epi16(QA);
    const Vectori16 VEC_ZERO = set1_epi16(0);

    Vectori32 accumulator{};

    #pragma unroll
    for (usize i = 0; i < HL_SIZE; i += VECTOR_SIZE) {
        // Load accumulator
        const Vectori16 accumValues  = load_epi16(&accum[i]);

        // Clamp values
        const Vectori16 clamped  = min_epi16(VEC_QA, max_epi16(accumValues, VEC_ZERO));

        // Load weights
        const Vectori16 weights  = load_epi16(reinterpret_cast<const Vectori16*>(&weightsToOut[i]));

        // SCReLU it
        const Vectori32 activated  = madd_epi16(clamped, mullo_epi16(clamped, weights));

        accumulator = add_epi32(accumulator, activated);
    }

    return reduce_epi32(accumulator);
}
#else
    #pragma message("Using compiler optimized NNUE inference")
i32 NN::vectorizedSCReLU(const Accumulator& accum) const {
    i32 res = 0;

    #pragma unroll
    for (usize i = 0; i < HL_SIZE; i++) {
        res += (i32) SCReLU(accum[i]) * weightsToOut[bucket][i];
    }
    return res;
}
#endif

// Finds the input feature
usize NN::feature(const Color stm, const Color pieceColor, const PieceType piece, const Square square) {
    const bool enemy = stm != pieceColor;
    const int squareIndex = (stm == BLACK) ? flipRank(square) : static_cast<int>(square);

    return enemy * 64 * 6 + piece * 64 + squareIndex;
}

i32 evaluate(const Board& board) {
    const Accumulator accum(board);
    i32 eval = 0;

    if constexpr (ACTIVATION != ::SCReLU) {
        for (usize i = 0; i < HL_SIZE; i++) {
            // First HL_SIZE weights are for STM
            if constexpr (ACTIVATION == ::ReLU)
                eval += nn.ReLU(accum[i]) * nn.weightsToOut[i];
            if constexpr (ACTIVATION == ::CReLU)
                eval += nn.CReLU(accum[i]) * nn.weightsToOut[i];
        }
    }
    else
        eval = nn.vectorizedSCReLU(accum);


    // Dequantization
    if constexpr (ACTIVATION == ::SCReLU)
        eval /= QA;

    eval += nn.outputBias;

    // Apply output bias and scale the result
    return (eval * EVAL_SCALE) / (QA * QB);
}