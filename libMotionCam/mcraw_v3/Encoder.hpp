#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

namespace motioncam {
namespace mcraw_v3 {

class Encoder {
public:
    static void encode(const uint16_t* input, size_t numPixels, std::vector<uint8_t>& output);

private:
    static void packRAW10_NEON(const uint16_t* input, size_t numPixels, uint8_t* output);
    static void packRAW10_Scalar(const uint16_t* input, size_t numPixels, uint8_t* output);
};

} // namespace mcraw_v3
} // namespace motioncam
