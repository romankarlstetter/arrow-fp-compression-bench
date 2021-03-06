#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>
#include <immintrin.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include <sys/mman.h>

#define ASSERT(x) \
    do { \
        if (!(x)) { \
            printf("%s\n", #x); \
            exit(-1); \
        } \
    } while(0)

#define _mm256_unpacklo_epi128(a, b) _mm256_permute2x128_si256(a, b, 2 << 4);
#define _mm256_unpackhi_epi128(a, b) _mm256_permute2x128_si256(a, b, 1 | (3 << 4));

enum RunName {
    RnStart = 0,

    RnMemcpy = RnStart,
    RnEncodeScalarFloat,
    RnDecodeScalarFloat,
    RnEncodeScalarDouble,
    RnDecodeScalarDouble,
    RnEncodeSimdFloat,
    RnDecodeSimdFloat,
    RnEncodeSimdDouble,
    RnDecodeSimdDouble,
#ifdef __AVX2__
    RnEncodeAVX2Float,
    RnDecodeAVX2Float,
    RnEncodeAVX2Double,
    RnDecodeAVX2Double,
#endif
    RnEnd,

};

const char* CovertRnNameToString(RunName name) {
    switch (name) {
        case RnMemcpy:
            return "memcpy";
        case RnEncodeScalarFloat:
            return "encode_scalar_float";
        case RnDecodeScalarFloat:
            return "decode_scalar_float";
        case RnEncodeScalarDouble:
            return "encode_scalar_double";
        case RnDecodeScalarDouble:
            return "decode_scalar_double";
        case RnEncodeSimdFloat:
            return "encode_simd_float";
        case RnDecodeSimdFloat:
            return "decode_simd_float";
        case RnEncodeSimdDouble:
            return "encode_simd_double";
        case RnDecodeSimdDouble:
            return "decode_simd_double";
#ifdef __AVX2__
        case RnEncodeAVX2Float:
            return "encode_avx2_float";
        case RnDecodeAVX2Float:
            return "decode_avx2_float";
        case RnEncodeAVX2Double:
            return "encode_avx2_double";
        case RnDecodeAVX2Double:
            return "decode_avx2_double";
#endif
        default:
            ASSERT(!"Unknown name");
            return NULL;
    }
}

static inline double gettime(void) {
    struct timespec ts = {0};
    int err = clock_gettime(CLOCK_MONOTONIC, &ts);
    (void)err;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

template<size_t type_size>
void decode_scalar(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    for (size_t i = 0; i < num_elements; ++i) {
        for (size_t k = 0; k < type_size; ++k) {
            output_data[i * type_size + k] = input_data[k * num_elements + i];
        }
    }
}

template<size_t type_size>
void encode_scalar(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    for (size_t i = 0; i < num_elements; ++i) {
        for (size_t k = 0; k < type_size; ++k) {
            output_data[k * num_elements + i] = input_data[i * type_size + k];
        }
    }
}

void encode_simd_float(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    // We will not handle the special case here and assume size is divisble 16 * 4.
    __m128i s[4];
    __m128i p[4];
    for (size_t i = 0; i < num_elements * 4UL; i += 64UL) {
        s[0] = _mm_loadu_si128((__m128i*)(input_data + i));
        s[1] = _mm_loadu_si128((__m128i*)(input_data + i + 16UL));
        s[2] = _mm_loadu_si128((__m128i*)(input_data + i + 32UL));
        s[3] = _mm_loadu_si128((__m128i*)(input_data + i + 48UL));

        p[0] = _mm_unpacklo_epi8(s[0], s[1]);
        p[1] = _mm_unpackhi_epi8(s[0], s[1]);
        p[2] = _mm_unpacklo_epi8(s[2], s[3]);
        p[3] = _mm_unpackhi_epi8(s[2], s[3]);

        s[0] = _mm_unpacklo_epi8(p[0], p[1]);
        s[1] = _mm_unpackhi_epi8(p[0], p[1]);
        s[2] = _mm_unpacklo_epi8(p[2], p[3]);
        s[3] = _mm_unpackhi_epi8(p[2], p[3]);

        p[0] = _mm_unpacklo_epi8(s[0], s[1]);
        p[1] = _mm_unpackhi_epi8(s[0], s[1]);
        p[2] = _mm_unpacklo_epi8(s[2], s[3]);
        p[3] = _mm_unpackhi_epi8(s[2], s[3]);

        s[0] = _mm_unpacklo_epi64(p[0], p[2]);
        s[1] = _mm_unpackhi_epi64(p[0], p[2]);
        s[2] = _mm_unpacklo_epi64(p[1], p[3]);
        s[3] = _mm_unpackhi_epi64(p[1], p[3]);

        size_t off = i / 4UL;
        _mm_storeu_si128((__m128i*)(output_data + off), s[0]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements + off), s[1]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*2 + off), s[2]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*3 + off), s[3]);
    }
}

void decode_simd_float(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    __m128i s[4];
    __m128i p[4];
    for (size_t i = 0; i < num_elements; i += 16UL) {
        s[0] = _mm_loadu_si128((__m128i*)(input_data + i));
        s[1] = _mm_loadu_si128((__m128i*)(input_data + num_elements + i));
        s[2] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 2UL + i));
        s[3] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 3UL + i));

        p[0] = _mm_unpacklo_epi8(s[0], s[2]);
        p[1] = _mm_unpackhi_epi8(s[0], s[2]);
        p[2] = _mm_unpacklo_epi8(s[1], s[3]);
        p[3] = _mm_unpackhi_epi8(s[1], s[3]);

        s[0] = _mm_unpacklo_epi8(p[0], p[2]);
        s[1] = _mm_unpackhi_epi8(p[0], p[2]);
        s[2] = _mm_unpacklo_epi8(p[1], p[3]);
        s[3] = _mm_unpackhi_epi8(p[1], p[3]);

        size_t off = i * 4UL;
        _mm_storeu_si128((__m128i*)(output_data + off), s[0]);
        _mm_storeu_si128((__m128i*)(output_data + off + 16UL), s[1]);
        _mm_storeu_si128((__m128i*)(output_data + off + 32UL), s[2]);
        _mm_storeu_si128((__m128i*)(output_data + off + 48UL), s[3]);
    }
}

void encode_simd_double(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    // We will not handle the special case here and assume size is divisble 16 * 8.
    __m128i s[8];
    __m128i p[8];
    for (size_t i = 0; i < num_elements * 8UL; i += 128UL) {
        s[0] = _mm_loadu_si128((__m128i*)(input_data + i));
        s[1] = _mm_loadu_si128((__m128i*)(input_data + i + 16UL));
        s[2] = _mm_loadu_si128((__m128i*)(input_data + i + 32UL));
        s[3] = _mm_loadu_si128((__m128i*)(input_data + i + 48UL));
        s[4] = _mm_loadu_si128((__m128i*)(input_data + i + 64UL));
        s[5] = _mm_loadu_si128((__m128i*)(input_data + i + 80UL));
        s[6] = _mm_loadu_si128((__m128i*)(input_data + i + 96UL));
        s[7] = _mm_loadu_si128((__m128i*)(input_data + i + 112UL));

        p[0] = _mm_unpacklo_epi8(s[0], s[1]);
        p[1] = _mm_unpackhi_epi8(s[0], s[1]);
        p[2] = _mm_unpacklo_epi8(s[2], s[3]);
        p[3] = _mm_unpackhi_epi8(s[2], s[3]);
        p[4] = _mm_unpacklo_epi8(s[4], s[5]);
        p[5] = _mm_unpackhi_epi8(s[4], s[5]);
        p[6] = _mm_unpacklo_epi8(s[6], s[7]);
        p[7] = _mm_unpackhi_epi8(s[6], s[7]);

        s[0] = _mm_unpacklo_epi8(p[0], p[1]);
        s[1] = _mm_unpackhi_epi8(p[0], p[1]);
        s[2] = _mm_unpacklo_epi8(p[2], p[3]);
        s[3] = _mm_unpackhi_epi8(p[2], p[3]);
        s[4] = _mm_unpacklo_epi8(p[4], p[5]);
        s[5] = _mm_unpackhi_epi8(p[4], p[5]);
        s[6] = _mm_unpacklo_epi8(p[6], p[7]);
        s[7] = _mm_unpackhi_epi8(p[6], p[7]);

        p[0] = _mm_unpacklo_epi32(s[0], s[4]);
        p[1] = _mm_unpackhi_epi32(s[0], s[4]);
        p[2] = _mm_unpacklo_epi32(s[1], s[5]);
        p[3] = _mm_unpackhi_epi32(s[1], s[5]);
        p[4] = _mm_unpacklo_epi32(s[2], s[6]);
        p[5] = _mm_unpackhi_epi32(s[2], s[6]);
        p[6] = _mm_unpacklo_epi32(s[3], s[7]);
        p[7] = _mm_unpackhi_epi32(s[3], s[7]);

        s[0] = _mm_unpacklo_epi32(p[0], p[4]);
        s[1] = _mm_unpackhi_epi32(p[0], p[4]);
        s[2] = _mm_unpacklo_epi32(p[1], p[5]);
        s[3] = _mm_unpackhi_epi32(p[1], p[5]);
        s[4] = _mm_unpacklo_epi32(p[2], p[6]);
        s[5] = _mm_unpackhi_epi32(p[2], p[6]);
        s[6] = _mm_unpacklo_epi32(p[3], p[7]);
        s[7] = _mm_unpackhi_epi32(p[3], p[7]);

        size_t off = i / 8UL;
        _mm_storeu_si128((__m128i*)(output_data + off), s[0]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements + off), s[1]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*2 + off), s[2]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*3 + off), s[3]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*4 + off), s[4]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*5 + off), s[5]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*6 + off), s[6]);
        _mm_storeu_si128((__m128i*)(output_data + num_elements*7 + off), s[7]);
    }
}

void decode_simd_double(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    __m128i s[8];
    __m128i p[8];
    for (size_t i = 0; i < num_elements; i += 16UL) {
        s[0] = _mm_loadu_si128((__m128i*)(input_data + i));
        s[1] = _mm_loadu_si128((__m128i*)(input_data + num_elements + i));
        s[2] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 2UL + i));
        s[3] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 3UL + i));
        s[4] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 4UL + i));
        s[5] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 5UL + i));
        s[6] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 6UL + i));
        s[7] = _mm_loadu_si128((__m128i*)(input_data + num_elements * 7UL + i));

        p[0] = _mm_unpacklo_epi8(s[0], s[4]);
        p[1] = _mm_unpackhi_epi8(s[0], s[4]);
        p[2] = _mm_unpacklo_epi8(s[1], s[5]);
        p[3] = _mm_unpackhi_epi8(s[1], s[5]);
        p[4] = _mm_unpacklo_epi8(s[2], s[6]);
        p[5] = _mm_unpackhi_epi8(s[2], s[6]);
        p[6] = _mm_unpacklo_epi8(s[3], s[7]);
        p[7] = _mm_unpackhi_epi8(s[3], s[7]);

        s[0] = _mm_unpacklo_epi8(p[0], p[4]);
        s[1] = _mm_unpackhi_epi8(p[0], p[4]);
        s[2] = _mm_unpacklo_epi8(p[1], p[5]);
        s[3] = _mm_unpackhi_epi8(p[1], p[5]);
        s[4] = _mm_unpacklo_epi8(p[2], p[6]);
        s[5] = _mm_unpackhi_epi8(p[2], p[6]);
        s[6] = _mm_unpacklo_epi8(p[3], p[7]);
        s[7] = _mm_unpackhi_epi8(p[3], p[7]);

        p[0] = _mm_unpacklo_epi8(s[0], s[4]);
        p[1] = _mm_unpackhi_epi8(s[0], s[4]);
        p[2] = _mm_unpacklo_epi8(s[1], s[5]);
        p[3] = _mm_unpackhi_epi8(s[1], s[5]);
        p[4] = _mm_unpacklo_epi8(s[2], s[6]);
        p[5] = _mm_unpackhi_epi8(s[2], s[6]);
        p[6] = _mm_unpacklo_epi8(s[3], s[7]);
        p[7] = _mm_unpackhi_epi8(s[3], s[7]);

        size_t off = i * 8UL;
        _mm_storeu_si128((__m128i*)(output_data + off), p[0]);
        _mm_storeu_si128((__m128i*)(output_data + off + 16UL), p[1]);
        _mm_storeu_si128((__m128i*)(output_data + off + 32UL), p[2]);
        _mm_storeu_si128((__m128i*)(output_data + off + 48UL), p[3]);
        _mm_storeu_si128((__m128i*)(output_data + off + 64UL), p[4]);
        _mm_storeu_si128((__m128i*)(output_data + off + 80UL), p[5]);
        _mm_storeu_si128((__m128i*)(output_data + off + 96UL), p[6]);
        _mm_storeu_si128((__m128i*)(output_data + off + 112UL), p[7]);
    }
}

#ifdef __AVX2__
void encode_avx2_float(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    // We will not handle the special case here and assume size is divisble 32 * 4.
    __m256i s[4];
    __m256i p[4];
    for (size_t i = 0; i < num_elements * 4UL; i += 128UL) {
        s[0] = _mm256_loadu_si256((__m256i*)(input_data + i));
        s[1] = _mm256_loadu_si256((__m256i*)(input_data + i + 32UL));
        s[2] = _mm256_loadu_si256((__m256i*)(input_data + i + 64UL));
        s[3] = _mm256_loadu_si256((__m256i*)(input_data + i + 96UL));

        p[0] = _mm256_unpacklo_epi8(s[0], s[1]);
        p[1] = _mm256_unpackhi_epi8(s[0], s[1]);
        p[2] = _mm256_unpacklo_epi8(s[2], s[3]);
        p[3] = _mm256_unpackhi_epi8(s[2], s[3]);

        s[0] = _mm256_unpacklo_epi8(p[0], p[1]);
        s[1] = _mm256_unpackhi_epi8(p[0], p[1]);
        s[2] = _mm256_unpacklo_epi8(p[2], p[3]);
        s[3] = _mm256_unpackhi_epi8(p[2], p[3]);

        p[0] = _mm256_unpacklo_epi8(s[0], s[1]);
        p[1] = _mm256_unpackhi_epi8(s[0], s[1]);
        p[2] = _mm256_unpacklo_epi8(s[2], s[3]);
        p[3] = _mm256_unpackhi_epi8(s[2], s[3]);

        s[0] = _mm256_unpacklo_epi128(p[0], p[2]);
        s[1] = _mm256_unpackhi_epi128(p[0], p[2]);
        s[2] = _mm256_unpacklo_epi128(p[1], p[3]);
        s[3] = _mm256_unpackhi_epi128(p[1], p[3]);

        p[0] = _mm256_unpacklo_epi32(s[0], s[1]);
        p[1] = _mm256_unpackhi_epi32(s[0], s[1]);
        p[2] = _mm256_unpacklo_epi32(s[2], s[3]);
        p[3] = _mm256_unpackhi_epi32(s[2], s[3]);

        size_t off = i / 4UL;
        _mm256_storeu_si256((__m256i*)(output_data + off), p[0]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements + off), p[1]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*2 + off), p[2]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*3 + off), p[3]);
    }
}

void decode_avx2_float(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    __m256i s[4];
    __m256i p[4];
    for (size_t i = 0; i < num_elements; i += 32UL) {
        s[0] = _mm256_loadu_si256((__m256i*)(input_data + i));
        s[1] = _mm256_loadu_si256((__m256i*)(input_data + num_elements + i));
        s[2] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 2UL + i));
        s[3] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 3UL + i));

        p[0] = _mm256_unpacklo_epi8(s[0], s[1]);
        p[1] = _mm256_unpackhi_epi8(s[0], s[1]);
        p[2] = _mm256_unpacklo_epi8(s[2], s[3]);
        p[3] = _mm256_unpackhi_epi8(s[2], s[3]);

        s[0] = _mm256_unpacklo_epi64(p[0], p[1]);
        s[1] = _mm256_unpackhi_epi64(p[0], p[1]);
        s[2] = _mm256_unpacklo_epi64(p[2], p[3]);
        s[3] = _mm256_unpackhi_epi64(p[2], p[3]);

        p[0] = _mm256_unpacklo_epi128(s[0], s[1]);
        p[1] = _mm256_unpackhi_epi128(s[0], s[1]);
        p[2] = _mm256_unpacklo_epi128(s[2], s[3]);
        p[3] = _mm256_unpackhi_epi128(s[2], s[3]);

        s[0] = _mm256_unpacklo_epi16(p[0], p[2]);
        s[1] = _mm256_unpackhi_epi16(p[0], p[2]);
        s[2] = _mm256_unpacklo_epi16(p[1], p[3]);
        s[3] = _mm256_unpackhi_epi16(p[1], p[3]);

        size_t off = i * 4UL;
        _mm256_storeu_si256((__m256i*)(output_data + off), s[0]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 32UL), s[1]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 64UL), s[2]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 96UL), s[3]);
    }
}

void encode_avx2_double(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    // We will not handle the special case here and assume size is divisble 32 * 8.
    __m256i s[8];
    __m256i p[8];
    for (size_t i = 0; i < num_elements * 8UL; i += 256UL) {
        s[0] = _mm256_loadu_si256((__m256i*)(input_data + i));
        s[1] = _mm256_loadu_si256((__m256i*)(input_data + i + 32UL));
        s[2] = _mm256_loadu_si256((__m256i*)(input_data + i + 64UL));
        s[3] = _mm256_loadu_si256((__m256i*)(input_data + i + 96UL));
        s[4] = _mm256_loadu_si256((__m256i*)(input_data + i + 128UL));
        s[5] = _mm256_loadu_si256((__m256i*)(input_data + i + 160UL));
        s[6] = _mm256_loadu_si256((__m256i*)(input_data + i + 192UL));
        s[7] = _mm256_loadu_si256((__m256i*)(input_data + i + 224UL));

        p[0] = _mm256_unpacklo_epi128(s[0], s[4]);
        p[1] = _mm256_unpackhi_epi128(s[0], s[4]);
        p[2] = _mm256_unpacklo_epi128(s[1], s[5]);
        p[3] = _mm256_unpackhi_epi128(s[1], s[5]);
        p[4] = _mm256_unpacklo_epi128(s[2], s[6]);
        p[5] = _mm256_unpackhi_epi128(s[2], s[6]);
        p[6] = _mm256_unpacklo_epi128(s[3], s[7]);
        p[7] = _mm256_unpackhi_epi128(s[3], s[7]);

        s[0] = _mm256_unpacklo_epi8(p[0], p[4]);
        s[1] = _mm256_unpackhi_epi8(p[0], p[4]);
        s[2] = _mm256_unpacklo_epi8(p[1], p[5]);
        s[3] = _mm256_unpackhi_epi8(p[1], p[5]);
        s[4] = _mm256_unpacklo_epi8(p[2], p[6]);
        s[5] = _mm256_unpackhi_epi8(p[2], p[6]);
        s[6] = _mm256_unpacklo_epi8(p[3], p[7]);
        s[7] = _mm256_unpackhi_epi8(p[3], p[7]);

        p[0] = _mm256_unpacklo_epi8(s[0], s[4]);
        p[1] = _mm256_unpackhi_epi8(s[0], s[4]);
        p[2] = _mm256_unpacklo_epi8(s[1], s[5]);
        p[3] = _mm256_unpackhi_epi8(s[1], s[5]);
        p[4] = _mm256_unpacklo_epi8(s[2], s[6]);
        p[5] = _mm256_unpackhi_epi8(s[2], s[6]);
        p[6] = _mm256_unpacklo_epi8(s[3], s[7]);
        p[7] = _mm256_unpackhi_epi8(s[3], s[7]);

        s[0] = _mm256_unpacklo_epi8(p[0], p[4]);
        s[1] = _mm256_unpackhi_epi8(p[0], p[4]);
        s[2] = _mm256_unpacklo_epi8(p[1], p[5]);
        s[3] = _mm256_unpackhi_epi8(p[1], p[5]);
        s[4] = _mm256_unpacklo_epi8(p[2], p[6]);
        s[5] = _mm256_unpackhi_epi8(p[2], p[6]);
        s[6] = _mm256_unpacklo_epi8(p[3], p[7]);
        s[7] = _mm256_unpackhi_epi8(p[3], p[7]);

        p[0] = _mm256_unpacklo_epi8(s[0], s[4]);
        p[1] = _mm256_unpackhi_epi8(s[0], s[4]);
        p[2] = _mm256_unpacklo_epi8(s[1], s[5]);
        p[3] = _mm256_unpackhi_epi8(s[1], s[5]);
        p[4] = _mm256_unpacklo_epi8(s[2], s[6]);
        p[5] = _mm256_unpackhi_epi8(s[2], s[6]);
        p[6] = _mm256_unpacklo_epi8(s[3], s[7]);
        p[7] = _mm256_unpackhi_epi8(s[3], s[7]);

        size_t off = i / 8UL;
        _mm256_storeu_si256((__m256i*)(output_data + off), p[0]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements + off), p[1]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*2 + off), p[2]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*3 + off), p[3]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*4 + off), p[4]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*5 + off), p[5]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*6 + off), p[6]);
        _mm256_storeu_si256((__m256i*)(output_data + num_elements*7 + off), p[7]);
    }
}

void decode_avx2_double(const uint8_t *input_data, size_t num_elements, uint8_t *output_data) {
    __m256i s[8];
    __m256i p[8];
    const int permute_mask = (3U << 6) | (1U << 4) | (2U << 2) | (0U << 0);
    for (size_t i = 0; i < num_elements; i += 32UL) {
        s[0] = _mm256_loadu_si256((__m256i*)(input_data + i));
        s[1] = _mm256_loadu_si256((__m256i*)(input_data + num_elements + i));
        s[2] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 2UL + i));
        s[3] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 3UL + i));
        s[4] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 4UL + i));
        s[5] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 5UL + i));
        s[6] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 6UL + i));
        s[7] = _mm256_loadu_si256((__m256i*)(input_data + num_elements * 7UL + i));

        for (int i = 0; i < 4; ++i) {
            p[2*i] = _mm256_unpacklo_epi128(s[i], s[i+4]);
            p[2*i+1] = _mm256_unpackhi_epi128(s[i], s[i+4]);
        }
        for (int i = 0; i < 4; ++i) {
            s[2*i] = _mm256_unpacklo_epi8(p[i], p[i+4]);
            s[2*i+1] = _mm256_unpackhi_epi8(p[i], p[i+4]);
        }
        for (int i = 0; i < 4; ++i) {
            p[2*i] = _mm256_unpacklo_epi8(s[i], s[i+4]);
            p[2*i+1] = _mm256_unpackhi_epi8(s[i], s[i+4]);
        }
        for (int i = 0; i < 8; ++i) {
            s[i] = _mm256_permute4x64_epi64(p[i], permute_mask);
        }
        for (int i = 0; i < 8; ++i) {
            p[i] = _mm256_shuffle_epi32(s[i], permute_mask);
        }

        size_t off = i * 8UL;
        _mm256_storeu_si256((__m256i*)(output_data + off), p[0]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 32UL), p[1]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 64UL), p[2]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 96UL), p[3]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 128UL), p[4]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 160UL), p[5]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 192UL), p[6]);
        _mm256_storeu_si256((__m256i*)(output_data + off + 224UL), p[7]);
    }
}
#endif

double benchmark_path(RunName name, const uint8_t *input, size_t num_bytes, uint8_t *output, const size_t num_runs)
{
    size_t num_elements;
    switch(name) {
        case RnEncodeScalarFloat:
        case RnDecodeScalarFloat:
        case RnEncodeSimdFloat:
        case RnDecodeSimdFloat:
#ifdef __AVX2__
        case RnEncodeAVX2Float:
        case RnDecodeAVX2Float:
#endif
            num_elements = num_bytes / 4UL;
            break;
        case RnEncodeScalarDouble:
        case RnDecodeScalarDouble:
        case RnEncodeSimdDouble:
        case RnDecodeSimdDouble:
#ifdef __AVX2__
        case RnEncodeAVX2Double:
        case RnDecodeAVX2Double:
#endif
            num_elements = num_bytes / 8UL;
            break;
        case RnMemcpy:
            num_elements = num_bytes;
            break;
        default:
            ASSERT(!"Unknown name");
            return .0;
    }
    // Warm-up the cache.
    memcpy(output, input, num_bytes);

    double total_time = .0;
    double min_time = 1e123;
    for (size_t i = 0; i < num_runs; ++i) {
        double t1 = gettime();
        switch(name) {
            case RnMemcpy:
                memcpy(output, input, num_bytes);
                break;
            case RnEncodeScalarFloat:
                encode_scalar<4>(input, num_elements, output);
                break;
            case RnDecodeScalarFloat:
                decode_scalar<4>(input, num_elements, output);
                break;
            case RnEncodeSimdFloat:
                encode_simd_float(input, num_elements, output);
                break;
            case RnDecodeSimdFloat:
                decode_simd_float(input, num_elements, output);
                break;
            case RnEncodeScalarDouble:
                encode_scalar<8>(input, num_elements, output);
                break;
            case RnDecodeScalarDouble:
                decode_scalar<8>(input, num_elements, output);
                break;
            case RnEncodeSimdDouble:
                encode_simd_double(input, num_elements, output);
                break;
            case RnDecodeSimdDouble:
                decode_simd_double(input, num_elements, output);
                break;
#ifdef __AVX2__
            case RnEncodeAVX2Float:
                encode_avx2_float(input, num_elements, output);
                break;
            case RnDecodeAVX2Float:
                decode_avx2_float(input, num_elements, output);
                break;
            case RnEncodeAVX2Double:
                encode_avx2_double(input, num_elements, output);
                break;
            case RnDecodeAVX2Double:
                decode_avx2_double(input, num_elements, output);
                break;
#endif
            default:
                ASSERT(!"Unknown name");
                return .0;
        }
        double t2 = gettime();
	const double time_delta = t2-t1;
        total_time += time_delta;
	min_time = std::min(min_time, time_delta);

    }
    const uint64_t total_bytes_processed = num_runs * num_bytes;
    const double avg_bytes_per_s = total_bytes_processed / total_time;
    const double max_bytes_per_s = num_bytes / min_time;
    const double avg_gibs_per_s = avg_bytes_per_s / (1024.0 * 1024.0 * 1024.0);
    const double max_gibs_per_s = max_bytes_per_s / (1024.0 * 1024.0 * 1024.0);
    return max_gibs_per_s;
    return avg_gibs_per_s;
}

void test_all_encodings() {
    const size_t num_bytes = 1024 * 1024;
    const size_t num_elements_float = num_bytes / 4UL;
    const size_t num_elements_double = num_bytes / 8UL;
    uint8_t *input = (uint8_t*)malloc(num_bytes);
    uint8_t *expected_output_float = (uint8_t*)malloc(num_bytes);
    uint8_t *expected_output_double = (uint8_t*)malloc(num_bytes);
    uint8_t *output = (uint8_t*)malloc(num_bytes);
    srand(1337);
    for (size_t i = 0; i < num_bytes; ++i) {
        input[i] = (uint8_t)rand();
    }
    encode_scalar<4>(input, num_elements_float, expected_output_float);
    encode_scalar<8>(input, num_elements_double, expected_output_double);

    // Check that the decode_scalar works correctly.
    decode_scalar<4>(expected_output_float, num_elements_float, output);
    if (memcmp(input, output, num_bytes)) {
        ASSERT(!"encode_float or decode_flaot failed");
    }
    decode_scalar<8>(expected_output_double, num_elements_double, output);
    if (memcmp(input, output, num_bytes)) {
        ASSERT(!"encode_double or decode_double failed");
    }

    // Check that encode_simd_float and decode_simd_float work correctly.
    encode_simd_float(input, num_elements_float, output);
    if (memcmp(expected_output_float, output, num_bytes)) {
        ASSERT(!"encode_simd_float failed");
    }
    decode_simd_float(expected_output_float, num_elements_float, output);
    if (memcmp(input, output, num_bytes)) {
        ASSERT(!"decode_simd_float failed");
    }

    // Check that encode_simd_double and decode_simd_double work correctly.
    encode_simd_double(input, num_elements_double, output);
    if (memcmp(expected_output_double, output, num_bytes)) {
        ASSERT(!"encode_simd_double failed");
    }
    decode_simd_double(expected_output_double, num_elements_double, output);
    if (memcmp(input, output, num_bytes)) {
        ASSERT(!"decode_simd_double failed");
    }

#ifdef __AVX2__
    // Check that encode_avx2_float and decode_avx2_float work correctly.
    encode_avx2_float(input, num_elements_float, output);
    if (memcmp(expected_output_float, output, num_bytes)) {
        ASSERT(!"encode_avx2_float failed");
    }
    decode_avx2_float(expected_output_float, num_elements_float, output);
    if (memcmp(input, output, num_bytes)) {
        ASSERT(!"decode_avx2_float failed");
    }

    // Check that encode_avx2_double and decode_avx2_double work correctly.
    encode_avx2_double(input, num_elements_double, output);
    if (memcmp(expected_output_double, output, num_bytes)) {
        ASSERT(!"encode_avx2_double failed");
    }
    decode_avx2_double(expected_output_double, num_elements_double, output);
    if (memcmp(input, output, num_bytes)) {
        for (size_t i = 0; i < 256; ++i) {
            printf("%u\n", output[i]);
        }
        ASSERT(!"decode_avx2_double failed");
    }
#endif
    free(input);
    free(output);
    free(expected_output_float);
    free(expected_output_double);
}

void benchmark_all_encodings() {
    const size_t max_num_bytes = 32 * 1024 * 1024;
    const size_t num_runs = 256;
    printf("Averaging over %zu runs.\n", num_runs);
    uint8_t *input = (uint8_t*)malloc(max_num_bytes);
    uint8_t *output = (uint8_t*)malloc(max_num_bytes);
    srand(1337);
    for (size_t i = 0; i < max_num_bytes; ++i) {
        input[i] = (uint8_t)rand();
    }
    for (size_t num_kbytes: std::vector<size_t>({32,64,96,128,196,256,512,768,1024,2048,4096,8*1024,12*1024,16*1024, 20*1024, 24*1024,32*1024})) {
        const size_t num_bytes = std::min(num_kbytes*1024, max_num_bytes);
        printf("Testing %zu KiB.\n", num_bytes/1024);
        for (size_t i = RnStart; i < RnEnd; ++i) {
            const RunName name = (RunName)i;
            const char *name_s = CovertRnNameToString(name);
            double avg_gibs_per_s = benchmark_path(name, input, num_bytes, output, num_runs);
            printf("%s: %lf GiB/s\n", name_s, avg_gibs_per_s);
        }
    }
    free(input);
    free(output);
}

int main() {
    test_all_encodings();
    benchmark_all_encodings();
    return 0;
}
