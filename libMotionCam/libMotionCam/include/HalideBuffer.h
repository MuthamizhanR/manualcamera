#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <cstring>

namespace Halide {
namespace Runtime {

template <typename T>
class Buffer {
public:
    struct Dimension {
        int extent() const { return mExtent; }
        int stride() const { return mStride; }
        int mExtent = 0;
        int mStride = 0;
    };

    Buffer()
        : mData(nullptr), mWidth(0), mHeight(0), mChannels(0), mD3(0),
          mStride0(0), mStride1(0), mStride2(0), mStride3(0),
          mDimensions(0), mOwnsData(false) {}

    // 1D owning
    explicit Buffer(int width)
        : mWidth(std::max(0, width)), mHeight(1), mChannels(1), mD3(1),
          mStride0(1), mStride1(std::max(0, width)), mStride2(std::max(0, width)), mStride3(std::max(0, width)),
          mDimensions(1), mOwnsData(true) {
        initOwningMemory();
    }

    // 2D owning
    Buffer(int width, int height)
        : mWidth(std::max(0, width)), mHeight(std::max(0, height)), mChannels(1), mD3(1),
          mStride0(1), mStride1(std::max(0, width)), mStride2(std::max(0, width) * std::max(0, height)),
          mStride3(std::max(0, width) * std::max(0, height)),
          mDimensions(2), mOwnsData(true) {
        initOwningMemory();
    }

    // 3D owning
    Buffer(int width, int height, int channels)
        : mWidth(std::max(0, width)), mHeight(std::max(0, height)), mChannels(std::max(0, channels)), mD3(1),
          mStride0(1), mStride1(std::max(0, width)), mStride2(std::max(0, width) * std::max(0, height)),
          mStride3(std::max(0, width) * std::max(0, height) * std::max(0, channels)),
          mDimensions(3), mOwnsData(true) {
        initOwningMemory();
    }

    // 4D owning
    Buffer(int width, int height, int channels, int d3)
        : mWidth(std::max(0, width)), mHeight(std::max(0, height)),
          mChannels(std::max(0, channels)), mD3(std::max(0, d3)),
          mStride0(1), mStride1(std::max(0, width)),
          mStride2(std::max(0, width) * std::max(0, height)),
          mStride3(std::max(0, width) * std::max(0, height) * std::max(0, channels)),
          mDimensions(4), mOwnsData(true) {
        initOwningMemory();
    }

    // 1D non-owning
    Buffer(T* data, int width)
        : mData(data), mWidth(std::max(0, width)), mHeight(1), mChannels(1), mD3(1),
          mStride0(1), mStride1(std::max(0, width)), mStride2(std::max(0, width)), mStride3(std::max(0, width)),
          mDimensions(1), mOwnsData(false) {}

    // 2D non-owning
    Buffer(T* data, int width, int height)
        : mData(data), mWidth(std::max(0, width)), mHeight(std::max(0, height)), mChannels(1), mD3(1),
          mStride0(1), mStride1(std::max(0, width)), mStride2(std::max(0, width) * std::max(0, height)),
          mStride3(std::max(0, width) * std::max(0, height)),
          mDimensions(2), mOwnsData(false) {}

    // 3D non-owning
    Buffer(T* data, int width, int height, int channels)
        : mData(data), mWidth(std::max(0, width)), mHeight(std::max(0, height)), mChannels(std::max(0, channels)), mD3(1),
          mStride0(1), mStride1(std::max(0, width)), mStride2(std::max(0, width) * std::max(0, height)),
          mStride3(std::max(0, width) * std::max(0, height) * std::max(0, channels)),
          mDimensions(3), mOwnsData(false) {}

    // 4D non-owning
    Buffer(T* data, int width, int height, int channels, int d3)
        : mData(data), mWidth(std::max(0, width)), mHeight(std::max(0, height)),
          mChannels(std::max(0, channels)), mD3(std::max(0, d3)),
          mStride0(1), mStride1(std::max(0, width)),
          mStride2(std::max(0, width) * std::max(0, height)),
          mStride3(std::max(0, width) * std::max(0, height) * std::max(0, channels)),
          mDimensions(4), mOwnsData(false) {}

    Buffer(const Buffer& other) {
        copyFrom(other);
    }

    Buffer(Buffer&& other) noexcept {
        moveFrom(std::move(other));
    }

    Buffer& operator=(const Buffer& other) {
        if (this != &other) {
            copyFrom(other);
        }
        return *this;
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            moveFrom(std::move(other));
        }
        return *this;
    }

    T* data() { return mData; }
    const T* data() const { return mData; }
    T* host_ptr() { return mData; }
    const T* host_ptr() const { return mData; }
    void* raw_buffer() { return nullptr; }
    const void* raw_buffer() const { return nullptr; }

    T* begin() { return mData; }
    T* end() { return mData ? mData + totalElements() : nullptr; }
    const T* begin() const { return mData; }
    const T* end() const { return mData ? mData + totalElements() : nullptr; }

    int width() const { return mWidth; }
    int height() const { return mHeight; }
    int channels() const { return mChannels; }
    int dimensions() const { return mDimensions; }

    size_t totalElements() const {
        return static_cast<size_t>(std::max(0, mWidth)) *
               std::max(1, mHeight) *
               std::max(1, mChannels) *
               std::max(1, mD3);
    }

    size_t size_in_bytes() const {
        return totalElements() * sizeof(T);
    }

    int stride(int d) const {
        if (d == 0) return mStride0;
        if (d == 1) return mStride1;
        if (d == 2) return mStride2;
        if (d == 3) return mStride3;
        return 0;
    }

    Dimension dim(int d) const {
        if (d == 0) return {mWidth, mStride0};
        if (d == 1) return {mHeight, mStride1};
        if (d == 2) return {mChannels, mStride2};
        if (d == 3) return {mD3, mStride3};
        return {0, 0};
    }

    void set_host_dirty() {}
    void device_sync() {}
    void* device_malloc(void* /*device_interface*/ = nullptr) { return nullptr; }

    void fill(T value) {
        if (mData && totalElements() > 0) {
            std::fill(mData, mData + totalElements(), value);
        }
    }

    Buffer<T> copy() const {
        Buffer<T> b;
        b.mWidth = mWidth;
        b.mHeight = mHeight;
        b.mChannels = mChannels;
        b.mD3 = mD3;
        b.mStride0 = mStride0;
        b.mStride1 = mStride1;
        b.mStride2 = mStride2;
        b.mStride3 = mStride3;
        b.mDimensions = mDimensions;
        b.mOwnsData = true;
        size_t total = totalElements();
        b.mBuffer.resize(total);
        b.mData = b.mBuffer.data();
        if (mData && total > 0) {
            std::copy(mData, mData + total, b.mData);
        }
        return b;
    }

    template<typename Func>
    void for_each_element(Func f) {
        for (int c = 0; c < std::max(1, mChannels); ++c) {
            for (int y = 0; y < std::max(1, mHeight); ++y) {
                for (int x = 0; x < std::max(1, mWidth); ++x) {
                    f(x, y, c);
                }
            }
        }
    }

    T& operator()(int x = 0, int y = 0, int c = 0, int d = 0) {
        static T sFallback{};
        if (!mData) return sFallback;
        size_t idx = static_cast<size_t>(d) * mStride3 +
                     static_cast<size_t>(c) * mStride2 +
                     static_cast<size_t>(y) * mStride1 +
                     static_cast<size_t>(x) * mStride0;
        if (idx < totalElements()) {
            return mData[idx];
        }
        return sFallback;
    }

    const T& operator()(int x = 0, int y = 0, int c = 0, int d = 0) const {
        static const T sFallback{};
        if (!mData) return sFallback;
        size_t idx = static_cast<size_t>(d) * mStride3 +
                     static_cast<size_t>(c) * mStride2 +
                     static_cast<size_t>(y) * mStride1 +
                     static_cast<size_t>(x) * mStride0;
        if (idx < totalElements()) {
            return mData[idx];
        }
        return sFallback;
    }

    static Buffer<T> make_interleaved(T* data, int width, int height, int channels) {
        Buffer<T> b(data, width, height, channels);
        b.mStride0 = channels;
        b.mStride1 = channels * width;
        b.mStride2 = 1;
        b.mStride3 = channels * width * height;
        return b;
    }

    static Buffer<T> make_interleaved(int width, int height, int channels) {
        Buffer<T> b(width, height, channels);
        b.mStride0 = channels;
        b.mStride1 = channels * width;
        b.mStride2 = 1;
        b.mStride3 = channels * width * height;
        return b;
    }

private:
    void initOwningMemory() {
        size_t total = totalElements();
        mBuffer.assign(total, T{});
        mData = mBuffer.empty() ? nullptr : mBuffer.data();
    }

    void copyFrom(const Buffer& other) {
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mChannels = other.mChannels;
        mD3 = other.mD3;
        mStride0 = other.mStride0;
        mStride1 = other.mStride1;
        mStride2 = other.mStride2;
        mStride3 = other.mStride3;
        mDimensions = other.mDimensions;
        mOwnsData = other.mOwnsData;

        if (other.mOwnsData) {
            mBuffer = other.mBuffer;
            mData = mBuffer.empty() ? nullptr : mBuffer.data();
        } else {
            mData = other.mData;
        }
    }

    void moveFrom(Buffer&& other) noexcept {
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mChannels = other.mChannels;
        mD3 = other.mD3;
        mStride0 = other.mStride0;
        mStride1 = other.mStride1;
        mStride2 = other.mStride2;
        mStride3 = other.mStride3;
        mDimensions = other.mDimensions;
        mOwnsData = other.mOwnsData;
        mBuffer = std::move(other.mBuffer);
        mData = mOwnsData ? (mBuffer.empty() ? nullptr : mBuffer.data()) : other.mData;

        other.mData = nullptr;
        other.mWidth = other.mHeight = other.mChannels = other.mD3 = 0;
        other.mOwnsData = false;
    }

    T* mData{nullptr};
    int mWidth{0};
    int mHeight{0};
    int mChannels{0};
    int mD3{0};
    int mStride0{0};
    int mStride1{0};
    int mStride2{0};
    int mStride3{0};
    int mDimensions{0};
    bool mOwnsData{false};
    std::vector<T> mBuffer;
};

} // namespace Runtime
} // namespace Halide
