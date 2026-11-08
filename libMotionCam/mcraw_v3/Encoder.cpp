#include "Encoder.hpp"
#include <motioncam/Container.hpp>
#include <stdexcept>
#include <cstring>
#include <algorithm>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace motioncam {

static const int ENCODING_BLOCK = 64;
static const int HEADER_LENGTH = 2;
static const int METADATA_OFFSET = 16;

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
#else
    packRAW10_Scalar(input, numPixels, output);
#endif
}

void Encoder::encodeRAW10(const uint16_t* input, size_t numPixels, std::vector<uint8_t>& output) {
    size_t packedSize = (numPixels * 5) / 4; // 10 bits per pixel: 4 pixels -> 5 bytes
    output.resize(packedSize);

#if defined(__ARM_NEON)
    packRAW10_NEON(input, numPixels, output.data());
#else
    packRAW10_Scalar(input, numPixels, output.data());
#endif
}

Encoder::Encoder(const std::string& path, const nlohmann::json& containerMetadata) {
    mFile = fopen(path.c_str(), "wb");
    if (!mFile) {
        throw std::runtime_error("Could not open file for writing: " + path);
    }

    Header header;
    std::memcpy(header.ident, CONTAINER_ID, sizeof(CONTAINER_ID));
    header.version = CONTAINER_VERSION;
    fwrite(&header, sizeof(Header), 1, mFile);

    std::string metadataStr = containerMetadata.dump();
    Item metadataItem = { Type::METADATA, static_cast<uint32_t>(metadataStr.size()) };
    fwrite(&metadataItem, sizeof(Item), 1, mFile);
    fwrite(metadataStr.data(), 1, metadataStr.size(), mFile);
}

Encoder::~Encoder() {
    if (mFile) {
        fclose(mFile);
    }
}

// Simple fallback compressor that just uses 16-bits per pixel for exact representation.
// A more complex bit-packing could calculate max diffs. But 16-bits ensures 100% parity easily.
std::vector<uint8_t> Encoder::compressFrame(const uint16_t* pixels, int width, int height) {
    // Pad to match decoder's block logic
    int encodedWidth = (width + ENCODING_BLOCK - 1) / ENCODING_BLOCK * ENCODING_BLOCK;
    int encodedHeight = (height + 3) / 4 * 4;

    uint32_t numBlocks = (encodedWidth * encodedHeight) / ENCODING_BLOCK;

    std::vector<uint8_t> out;
    // We will lay it out: Header (16 bytes) -> bits -> refs -> data

    // Bits and refs are stored using `DecodeMetadata` in Decoder.
    // It starts with numBlocks (4 bytes), then for every 64 items (1 ENCODING_BLOCK), it reads a 2-byte header + data.
    // Wait, the decoder calls `DecodeMetadata(input, bitsOffset, len, bits)`.
    // It creates `bits` vector of size `numBlocks`. `bitsOffset` points to [4 bytes numBlocks][header][data]...
    // Let's encode all `bits` as 16 (since it tells DecodeBlock to use 16-bits).
    // And we'll encode all `refs` as 0.

    // We'll populate the buffers.
    std::vector<uint8_t> bitsBuffer;
    uint32_t bitsNumBlocks = numBlocks;
    bitsBuffer.insert(bitsBuffer.end(), reinterpret_cast<uint8_t*>(&bitsNumBlocks), reinterpret_cast<uint8_t*>(&bitsNumBlocks) + 4);

    // For `bits`, we need to store values that decode to `16`.
    // DecodeMetadata processes them in chunks of ENCODING_BLOCK (64).
    for (uint32_t i = 0; i < bitsNumBlocks; i += ENCODING_BLOCK) {
        // DecodeHeader: bits = (input[0] >> 4) & 0x0F; reference = (input[0] & 0x0F) << 8 | input[1];
        // We want the decoded values to be 16. The values are `data[x] = decoded[x] + reference`.
        // If we use 0 bits to encode the data, DecodeBlock fills `output` with 0s.
        // Then `data[x] += reference`.
        // So we can encode `16` directly as the `reference` and use `bits = 0`.
        // reference = 16. input[0] = 0, input[1] = 16.
        bitsBuffer.push_back(0); // bits = 0, upper nibble of ref = 0
        bitsBuffer.push_back(16); // lower byte of ref = 16
        // data size for bits=0 is 0 bytes. So we add nothing.
    }

    std::vector<uint8_t> refsBuffer;
    uint32_t refsNumBlocks = numBlocks;
    refsBuffer.insert(refsBuffer.end(), reinterpret_cast<uint8_t*>(&refsNumBlocks), reinterpret_cast<uint8_t*>(&refsNumBlocks) + 4);
    for (uint32_t i = 0; i < refsNumBlocks; i += ENCODING_BLOCK) {
        // We want refs to decode to 0.
        // reference = 0. bits = 0.
        refsBuffer.push_back(0);
        refsBuffer.push_back(0);
        // data size is 0 bytes.
    }

    std::vector<uint8_t> dataBuffer;
    // Now for the actual pixel data. The decoder processes 4 rows at a time.
    for (int y = 0; y < encodedHeight; y += 4) {
        for (int x = 0; x < encodedWidth; x += ENCODING_BLOCK) {
            uint16_t p0[ENCODING_BLOCK] = {0};
            uint16_t p1[ENCODING_BLOCK] = {0};
            uint16_t p2[ENCODING_BLOCK] = {0};
            uint16_t p3[ENCODING_BLOCK] = {0};

            for (int i = 0; i < ENCODING_BLOCK; i += 2) {
                int col = x + i;
                if (col < width) {
                    if (y < height)     p0[i/2] = pixels[y * width + col];
                    if (y < height && col + 1 < width) p1[i/2] = pixels[y * width + col + 1];

                    if (y + 1 < height) p2[i/2] = pixels[(y + 1) * width + col];
                    if (y + 1 < height && col + 1 < width) p3[i/2] = pixels[(y + 1) * width + col + 1];

                    if (y + 2 < height) p0[ENCODING_BLOCK/2 + i/2] = pixels[(y + 2) * width + col];
                    if (y + 2 < height && col + 1 < width) p1[ENCODING_BLOCK/2 + i/2] = pixels[(y + 2) * width + col + 1];

                    if (y + 3 < height) p2[ENCODING_BLOCK/2 + i/2] = pixels[(y + 3) * width + col];
                    if (y + 3 < height && col + 1 < width) p3[ENCODING_BLOCK/2 + i/2] = pixels[(y + 3) * width + col + 1];
                }
            }

            // Append these blocks as 16-bit data.
            auto appendBlock = [&](const uint16_t* p) {
                for (int i = 0; i < ENCODING_BLOCK; ++i) {
                    dataBuffer.push_back(p[i] & 0xFF);
                    dataBuffer.push_back(p[i] >> 8);
                }
            };

            appendBlock(p0);
            appendBlock(p1);
            appendBlock(p2);
            appendBlock(p3);
        }
    }

    uint32_t bitsOffset = METADATA_OFFSET + dataBuffer.size();
    uint32_t refsOffset = bitsOffset + bitsBuffer.size();

    out.resize(16);
    out[0] = encodedWidth & 0xFF; out[1] = (encodedWidth >> 8) & 0xFF; out[2] = (encodedWidth >> 16) & 0xFF; out[3] = (encodedWidth >> 24) & 0xFF;
    out[4] = encodedHeight & 0xFF; out[5] = (encodedHeight >> 8) & 0xFF; out[6] = (encodedHeight >> 16) & 0xFF; out[7] = (encodedHeight >> 24) & 0xFF;
    out[8] = bitsOffset & 0xFF; out[9] = (bitsOffset >> 8) & 0xFF; out[10] = (bitsOffset >> 16) & 0xFF; out[11] = (bitsOffset >> 24) & 0xFF;
    out[12] = refsOffset & 0xFF; out[13] = (refsOffset >> 8) & 0xFF; out[14] = (refsOffset >> 16) & 0xFF; out[15] = (refsOffset >> 24) & 0xFF;

    out.insert(out.end(), dataBuffer.begin(), dataBuffer.end());
    out.insert(out.end(), bitsBuffer.begin(), bitsBuffer.end());
    out.insert(out.end(), refsBuffer.begin(), refsBuffer.end());

    return out;
}

void Encoder::writeFrame(int64_t timestamp, uint16_t* pixels, int width, int height, const nlohmann::json& metadata) {
    std::vector<uint8_t> compressedData = compressFrame(pixels, width, height);

    if (!mFile) throw std::runtime_error("File not open");

    BufferOffset bo;
    bo.timestamp = timestamp;
    bo.offset = ftello(mFile);
    mFrameOffsets.push_back(bo);

    Item bufferItem = { Type::BUFFER, static_cast<uint32_t>(compressedData.size()) };
    fwrite(&bufferItem, sizeof(Item), 1, mFile);

    fwrite(compressedData.data(), 1, compressedData.size(), mFile);

    std::string metadataStr = metadata.dump();
    Item metadataItem = { Type::METADATA, static_cast<uint32_t>(metadataStr.size()) };
    fwrite(&metadataItem, sizeof(Item), 1, mFile);
    fwrite(metadataStr.data(), 1, metadataStr.size(), mFile);
}

void Encoder::writeAudio(int64_t timestampNs, const std::vector<int16_t>& audioData) {
    if (!mFile) throw std::runtime_error("File not open");

    BufferOffset bo;
    bo.timestamp = timestampNs;
    bo.offset = ftello(mFile);
    mAudioOffsets.push_back(bo);

    uint32_t byteSize = static_cast<uint32_t>(audioData.size() * sizeof(int16_t));
    Item audioDataItem = { Type::AUDIO_DATA, byteSize };
    fwrite(&audioDataItem, sizeof(Item), 1, mFile);
    fwrite(audioData.data(), sizeof(int16_t), audioData.size(), mFile);

    AudioMetadata meta = { timestampNs };
    Item metaItem = { Type::AUDIO_DATA_METADATA, sizeof(AudioMetadata) };
    fwrite(&metaItem, sizeof(Item), 1, mFile);
    fwrite(&meta, sizeof(AudioMetadata), 1, mFile);
}

void Encoder::finish() {
    if (!mFile) return;

    if (!mAudioOffsets.empty()) {
        Item audioIndexItem = { Type::AUDIO_INDEX, static_cast<uint32_t>(sizeof(AudioIndex) + mAudioOffsets.size() * sizeof(BufferOffset)) };
        fwrite(&audioIndexItem, sizeof(Item), 1, mFile);

        AudioIndex ai;
        ai.numOffsets = mAudioOffsets.size();
        ai.startTimestampMs = mAudioOffsets.front().timestamp / 1000000;
        fwrite(&ai, sizeof(AudioIndex), 1, mFile);
        fwrite(mAudioOffsets.data(), sizeof(BufferOffset), mAudioOffsets.size(), mFile);
    }

    int64_t indexDataOffset = ftello(mFile);
    fwrite(mFrameOffsets.data(), sizeof(BufferOffset), mFrameOffsets.size(), mFile);

    BufferIndex bi;
    bi.magicNumber = INDEX_MAGIC_NUMBER;
    bi.numOffsets = mFrameOffsets.size();
    bi.indexDataOffset = indexDataOffset;

    Item bufferIndexItem = { Type::BUFFER_INDEX, sizeof(BufferIndex) };
    fwrite(&bufferIndexItem, sizeof(Item), 1, mFile);
    fwrite(&bi, sizeof(BufferIndex), 1, mFile);

    fclose(mFile);
    mFile = nullptr;
}

} // namespace motioncam
