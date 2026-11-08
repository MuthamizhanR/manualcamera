#ifndef Encoder_hpp
#define Encoder_hpp

#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <nlohmann/json.hpp>

namespace motioncam {

class Encoder {
public:
    Encoder(const std::string& path, const nlohmann::json& containerMetadata);
    ~Encoder();

    // Compresses a 16-bit Bayer frame into the internal MotionCam bit-packed format
    // and writes it to the container.
    void writeFrame(int64_t timestamp, uint16_t* pixels, int width, int height, const nlohmann::json& metadata);

    // Write an audio chunk to the container.
    void writeAudio(int64_t timestampNs, const std::vector<int16_t>& audioData);

    // Call finish to write all the indices and cleanly close the file
    void finish();

    // Expose compression logic for unit testing
    static std::vector<uint8_t> compressFrame(const uint16_t* pixels, int width, int height);

    // Bit-packing optimization for 10-bit raw pixels
    static void packRAW10_NEON(const uint16_t* input, size_t numPixels, uint8_t* output);
    static void packRAW10_Scalar(const uint16_t* input, size_t numPixels, uint8_t* output);
    static void encodeRAW10(const uint16_t* input, size_t numPixels, std::vector<uint8_t>& output);

private:
    FILE* mFile;

    struct BufferOffset {
        int64_t offset;
        int64_t timestamp;
    };

    std::vector<BufferOffset> mFrameOffsets;
    std::vector<BufferOffset> mAudioOffsets;
};

namespace mcraw_v3 {

class Encoder {
public:
    static void encode(const uint16_t* input, size_t numPixels, std::vector<uint8_t>& output) {
        motioncam::Encoder::encodeRAW10(input, numPixels, output);
    }
};

} // namespace mcraw_v3

} // namespace motioncam

#endif /* Encoder_hpp */
