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
    Buffer() : mData(nullptr), mWidth(0), mHeight(0), mChannels(0), mStride0(0), mStride1(0), mStride2(0) {}

    Buffer(T* data, int width, int height, int channels = 1)
        : mData(data), mWidth(width), mHeight(height), mChannels(channels), mOwnsData(false) {
        mStride0 = 1;
        mStride1 = width;
        mStride2 = width * height;
    }

    Buffer(int width, int height, int channels = 1)
        : mWidth(width), mHeight(height), mChannels(channels), mOwnsData(true) {
        mStride0 = 1;
        mStride1 = width;
        mStride2 = width * height;
        mBuffer.resize(width * height * channels);
        mData = mBuffer.data();
    }

    Buffer(const Buffer& other) {
        copyFrom(other);
    }

    Buffer& operator=(const Buffer& other) {
        if (this != &other) {
            copyFrom(other);
        }
        return *this;
    }

    T* data() { return mData; }
    const T* data() const { return mData; }

    T* begin() { return mData; }
    T* end() { return mData + (mWidth * mHeight * mChannels); }

    int width() const { return mWidth; }
    int height() const { return mHeight; }
    int channels() const { return mChannels; }

    int stride(int dim) const {
        if (dim == 0) return mStride0;
        if (dim == 1) return mStride1;
        if (dim == 2) return mStride2;
        return 0;
    }

    void set_host_dirty() {}
    void device_sync() {}
    void fill(T value) {
        if (mData) {
            std::fill(mData, mData + (mWidth * mHeight * mChannels), value);
        }
    }

    Buffer<T> copy() const {
        return Buffer<T>(*this);
    }

    template<typename Func>
    void for_each_element(Func f) {
        for (int c = 0; c < mChannels; ++c) {
            for (int y = 0; y < mHeight; ++y) {
                for (int x = 0; x < mWidth; ++x) {
                    f(x, y, c);
                }
            }
        }
    }

    T& operator()(int x, int y, int c = 0) {
        return mData[c * mStride2 + y * mStride1 + x * mStride0];
    }

    const T& operator()(int x, int y, int c = 0) const {
        return mData[c * mStride2 + y * mStride1 + x * mStride0];
    }

    static Buffer<T> make_interleaved(T* data, int width, int height, int channels) {
        Buffer<T> b(data, width, height, channels);
        b.mStride0 = channels;
        b.mStride1 = channels * width;
        b.mStride2 = 1;
        return b;
    }

    static Buffer<T> make_interleaved(int width, int height, int channels) {
        Buffer<T> b(width, height, channels);
        b.mStride0 = channels;
        b.mStride1 = channels * width;
        b.mStride2 = 1;
        return b;
    }

private:
    void copyFrom(const Buffer& other) {
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mChannels = other.mChannels;
        mStride0 = other.mStride0;
        mStride1 = other.mStride1;
        mStride2 = other.mStride2;
        mOwnsData = other.mOwnsData;

        if (other.mOwnsData) {
            mBuffer = other.mBuffer;
            mData = mBuffer.data();
        } else {
            mData = other.mData;
        }
    }

    T* mData;
    int mWidth;
    int mHeight;
    int mChannels;
    int mStride0;
    int mStride1;
    int mStride2;
    bool mOwnsData;
    std::vector<T> mBuffer;
};

} // namespace Runtime
} // namespace Halide
