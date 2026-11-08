#include "Encoder.hpp"
#include <motioncam/Container.hpp>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace motioncam {

static const int ENCODING_BLOCK = 64;
static const int HEADER_LENGTH = 2;
static const int METADATA_OFFSET = 16;

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
            // It reads 4 blocks, each corresponding to a bit size (which we set to 16).
            // Block 0: row 0 and 2, left half.
            // Block 1: row 0 and 2, right half.
            // Block 2: row 1 and 3, left half.
            // Block 3: row 1 and 3, right half.

            // Wait, looking at the decode logic:
            // p0: block 0 (bits[0]), p1: block 1 (bits[1]), p2: block 2 (bits[2]), p3: block 3 (bits[3]).
            // p0[0..31] goes to row0[x + 0..63 (evens)], wait.
            // i goes from 0 to 62, step 2.
            // row0[x + i] = p0[i/2], row0[x + i + 1] = p1[i/2].
            // So p0 contains evens, p1 contains odds.

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
