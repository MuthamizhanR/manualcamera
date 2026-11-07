#include "motioncam/CameraPreview.h"
#include "motioncam/RawImageMetadata.h"
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include "motioncam/ImageProcessor.h"
#include <algorithm>

namespace motioncam {
    void CameraPreview::generate(const RawImageBuffer& rawBuffer,
                                 const RawCameraMetadata& cameraMetadata,
                                 const int downscaleFactor,
                                 const bool flipped,
                                 const float shadows,
                                 const float contrast,
                                 const float saturation,
                                 const float blacks,
                                 const float whitePoint,
                                 const float temperatureOffset,
                                 const float tintOffset,
                                 const float tonemapVariance,
                                 Halide::Runtime::Buffer<uint8_t>& inputBuffer,
                                 Halide::Runtime::Buffer<uint8_t>& outputBuffer)
    {
        if (downscaleFactor <= 0 || rawBuffer.width <= 0 || rawBuffer.height <= 0) {
            return;
        }

        int width = rawBuffer.width / 2 / downscaleFactor;
        int height = rawBuffer.height / 2 / downscaleFactor;

        if (width <= 0 || height <= 0) {
            return;
        }

        uint8_t* out_data = outputBuffer.data();
        if (!out_data) {
            return;
        }

        const size_t outCapacity = outputBuffer.size_in_bytes();
        const size_t requiredOutBytes = static_cast<size_t>(width) * height * 4;
        if (outCapacity < requiredOutBytes) {
            return;
        }

        const uint8_t* in_raw = inputBuffer.data();
        const size_t inSize = inputBuffer.size_in_bytes();

        if (in_raw != nullptr && inSize >= static_cast<size_t>(rawBuffer.width) * rawBuffer.height) {
            const uint16_t* in16 = reinterpret_cast<const uint16_t*>(in_raw);
            const bool is16Bit = (rawBuffer.pixelFormat == PixelFormat::RAW16) &&
                                 (inSize >= static_cast<size_t>(rawBuffer.width) * rawBuffer.height * 2);

            for (int y = 0; y < height; ++y) {
                int srcY = std::min(rawBuffer.height - 1, y * 2 * downscaleFactor);
                for (int x = 0; x < width; ++x) {
                    int srcX = std::min(rawBuffer.width - 1, x * 2 * downscaleFactor);
                    size_t out_idx = static_cast<size_t>(y * width + x) * 4;
                    if (out_idx + 3 >= outCapacity) break;

                    uint8_t val = 0;
                    if (is16Bit) {
                        size_t srcIdx = static_cast<size_t>(srcY) * rawBuffer.width + srcX;
                        val = static_cast<uint8_t>(std::min(255, in16[srcIdx] >> 8));
                    } else {
                        size_t srcIdx = static_cast<size_t>(srcY) * rawBuffer.width + srcX;
                        if (srcIdx < inSize) {
                            val = in_raw[srcIdx];
                        }
                    }

                    out_data[out_idx] = val;
                    out_data[out_idx + 1] = val;
                    out_data[out_idx + 2] = val;
                    out_data[out_idx + 3] = 255;
                }
            }
        } else {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    size_t out_idx = static_cast<size_t>(y * width + x) * 4;
                    if (out_idx + 3 >= outCapacity) break;
                    out_data[out_idx] = 16;
                    out_data[out_idx + 1] = 16;
                    out_data[out_idx + 2] = 16;
                    out_data[out_idx + 3] = 255;
                }
            }
        }
    }
}
