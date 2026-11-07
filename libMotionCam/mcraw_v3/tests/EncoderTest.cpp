#include "../Encoder.hpp"
#include <motioncam/Decoder.hpp>
#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>
#include <random>

using namespace motioncam;

void testEncoderAndDecoderParity() {
    std::string testFile = "test_output.mcraw";

    nlohmann::json containerMeta = {
        {"camera", "TestCamera123"},
        {"version", "1.0"}
    };

    int width = 128; // must be a multiple of 64 or handled correctly
    int height = 128;

    std::vector<uint16_t> syntheticPixels(width * height);
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint16_t> dist(0, 4095);
    for (auto& p : syntheticPixels) p = dist(rng);

    nlohmann::json frameMeta = {
        {"width", width},
        {"height", height},
        {"compressionType", 7}, // MOTIONCAM_COMPRESSION_TYPE
        {"whiteBalance", {1.0, 1.0, 1.0, 1.0}}
    };

    // Write
    {
        Encoder encoder(testFile, containerMeta);
        encoder.writeFrame(1000, syntheticPixels.data(), width, height, frameMeta);

        std::vector<int16_t> audioData = {1, -2, 3, -4, 5};
        encoder.writeAudio(1000, audioData);

        encoder.finish();
    }

    // Read and verify
    {
        Decoder decoder(testFile);

        auto decodedMeta = decoder.getContainerMetadata();
        assert(decodedMeta["camera"] == "TestCamera123");

        const auto& frames = decoder.getFrames();
        assert(frames.size() == 1);
        assert(frames[0] == 1000);

        std::vector<uint8_t> outData;
        nlohmann::json outFrameMeta;
        decoder.loadFrame(1000, outData, outFrameMeta);

        assert(outFrameMeta["compressionType"] == 7);
        assert(outData.size() == width * height * 2);

        uint16_t* decodedPixels = reinterpret_cast<uint16_t*>(outData.data());
        for (int i = 0; i < width * height; ++i) {
            if (decodedPixels[i] != syntheticPixels[i]) {
                std::cerr << "Mismatch at pixel " << i << ": expected " << syntheticPixels[i] << ", got " << decodedPixels[i] << std::endl;
                assert(false);
            }
        }

        std::vector<AudioChunk> chunks;
        decoder.loadAudio(chunks);
        assert(chunks.size() == 1);
        assert(chunks[0].first == 1000);
        assert(chunks[0].second.size() == 5);
        assert(chunks[0].second[0] == 1);
        assert(chunks[0].second[4] == 5);
    }

    std::cout << "All tests passed!" << std::endl;
}

int main() {
    testEncoderAndDecoderParity();
    return 0;
}
