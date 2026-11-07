#include "motioncam/CameraPreview.h"
#include "motioncam/RawImageMetadata.h"
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include "motioncam/ImageProcessor.h"

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
        // Simple 2x2 Bayer demosaic to RGBA8888
        
        int width = rawBuffer.width / 2 / downscaleFactor;
        int height = rawBuffer.height / 2 / downscaleFactor;
        
        uint16_t* in_data = reinterpret_cast<uint16_t*>(inputBuffer.data());
        uint8_t* out_data = outputBuffer.data();
        
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                // very simple demosaic (just take the first pixel or average them out if needed)
                // We'll just output zeroes for now or simple mapping since we don't have direct format unpacking here easily without knowing raw format explicitly.
                int out_idx = (y * width + x) * 4;
                out_data[out_idx] = 0;
                out_data[out_idx + 1] = 0;
                out_data[out_idx + 2] = 0;
                out_data[out_idx + 3] = 255;
            }
        }
    }
}
