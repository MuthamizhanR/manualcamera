#include "Encoder.hpp"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace motioncam {
namespace mcraw_v3 {

void Encoder::encode(const uint16_t* input, size_t numPixels, std::vector<uint8_t>& output) {
    size_t packedSize = (numPixels * 5) / 4; // 10 bits per pixel: 4 pixels -> 5 bytes
    output.resize(packedSize);

#if defined(__ARM_NEON)
    packRAW10_NEON(input, numPixels, output.data());
#else
    packRAW10_Scalar(input, numPixels, output.data());
#endif
}

void Encoder::packRAW10_Scalar(const uint16_t* input, size_t numPixels, uint8_t* output) {
    for (size_t i = 0, j = 0; i < numPixels; i += 4, j += 5) {
        uint16_t p0 = input[i];
        uint16_t p1 = input[i + 1];
        uint16_t p2 = input[i + 2];
        uint16_t p3 = input[i + 3];

        output[j]     = p0 >> 2;
        output[j + 1] = p1 >> 2;
        output[j + 2] = p2 >> 2;
        output[j + 3] = p3 >> 2;
        output[j + 4] = ((p0 & 0x03) << 6) | ((p1 & 0x03) << 4) | ((p2 & 0x03) << 2) | (p3 & 0x03);
    }
}

void Encoder::packRAW10_NEON(const uint16_t* input, size_t numPixels, uint8_t* output) {
#if defined(__ARM_NEON)
    // 8 pixels at a time
    size_t i = 0;
    size_t j = 0;
    size_t limit = numPixels - (numPixels % 8);

    for (; i < limit; i += 8, j += 10) {
        uint16x8_t pixels = vld1q_u16(input + i);

        // Extract upper 8 bits
        uint8x8_t upper = vmovn_u16(vshrq_n_u16(pixels, 2));

        // Extract lower 2 bits
        uint16x8_t lower = vandq_u16(pixels, vdupq_n_u16(0x03));

        // Get the lower bits of the first 4 pixels
        uint16_t l0 = vgetq_lane_u16(lower, 0);
        uint16_t l1 = vgetq_lane_u16(lower, 1);
        uint16_t l2 = vgetq_lane_u16(lower, 2);
        uint16_t l3 = vgetq_lane_u16(lower, 3);

        // Get the lower bits of the second 4 pixels
        uint16_t l4 = vgetq_lane_u16(lower, 4);
        uint16_t l5 = vgetq_lane_u16(lower, 5);
        uint16_t l6 = vgetq_lane_u16(lower, 6);
        uint16_t l7 = vgetq_lane_u16(lower, 7);

        uint8_t packed_lower_1 = (l0 << 6) | (l1 << 4) | (l2 << 2) | l3;
        uint8_t packed_lower_2 = (l4 << 6) | (l5 << 4) | (l6 << 2) | l7;

        output[j]     = vget_lane_u8(upper, 0);
        output[j + 1] = vget_lane_u8(upper, 1);
        output[j + 2] = vget_lane_u8(upper, 2);
        output[j + 3] = vget_lane_u8(upper, 3);
        output[j + 4] = packed_lower_1;

        output[j + 5] = vget_lane_u8(upper, 4);
        output[j + 6] = vget_lane_u8(upper, 5);
        output[j + 7] = vget_lane_u8(upper, 6);
        output[j + 8] = vget_lane_u8(upper, 7);
        output[j + 9] = packed_lower_2;
    }

    // Process remaining pixels using scalar fallback
    packRAW10_Scalar(input + i, numPixels - i, output + j);
#endif
}

} // namespace mcraw_v3
} // namespace motioncam
