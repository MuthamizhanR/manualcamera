#include "Encoder.hpp"
#include "WriterQueue.hpp"
#include <iostream>
#include <vector>
#include <cassert>

using namespace motioncam::mcraw_v3;

void test_encoder() {
    std::vector<uint16_t> input = { 1023, 511, 255, 127, 0, 1, 2, 3 };
    std::vector<uint8_t> output;

    Encoder::encode(input.data(), 8, output);

    assert(output.size() == 10);

    // Verify first 4 pixels
    assert(output[0] == 255); // 1023 >> 2
    assert(output[1] == 127); // 511 >> 2
    assert(output[2] == 63);  // 255 >> 2
    assert(output[3] == 31);  // 127 >> 2
    assert(output[4] == 0xFF); // All lower 2 bits are 11

    // Verify second 4 pixels
    assert(output[5] == 0); // 0 >> 2
    assert(output[6] == 0); // 1 >> 2
    assert(output[7] == 0); // 2 >> 2
    assert(output[8] == 0); // 3 >> 2
    assert(output[9] == ((0 << 6) | (1 << 4) | (2 << 2) | 3)); // Lower 2 bits

    std::cout << "Encoder test passed.\n";
}

void test_writer_queue() {
    int count = 0;
    auto writerFunc = [&count](const WriterQueue::DataBuffer& data) {
        count += data.size();
    };

    WriterQueue queue(writerFunc);
    queue.enqueue({1, 2, 3});
    queue.enqueue({4, 5});

    queue.stop();

    assert(count == 5);
    std::cout << "Writer queue test passed.\n";
}

int main() {
    test_encoder();
    test_writer_queue();
    return 0;
}
