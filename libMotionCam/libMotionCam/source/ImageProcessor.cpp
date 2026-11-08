#include "motioncam/ImageProcessor.h"
#include "motioncam/RawContainer.h"
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include "motioncam/Exceptions.h"
#include "motioncam/Util.h"
#include "motioncam/Logger.h"
#include "motioncam/Measure.h"
#include "motioncam/Settings.h"
#include "motioncam/ImageOps.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <memory>

#include <exiv2/exiv2.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/xfeatures2d.hpp>

#ifdef DNG_SUPPORT

#include <dng/dng_host.h>
#include <dng/dng_negative.h>
#include <dng/dng_camera_profile.h>
#include <dng/dng_file_stream.h>
#include <dng/dng_memory_stream.h>
#include <dng/dng_image_writer.h>
#include <dng/dng_render.h>
#include <dng/dng_gain_map.h>

#endif

using std::ios;
using std::string;
using std::shared_ptr;
using std::vector;
using std::to_string;
using std::pair;







namespace motioncam {
    const int DENOISE_LEVELS            = 6;
    const int EXPANDED_RANGE            = 16384;
    const float MAX_HDR_ERROR           = 0.03f;
    const float WHITEPOINT_THRESHOLD    = 0.9999f;
    const float SHADOW_BIAS             = 16.0f;

    typedef Halide::Runtime::Buffer<float> WaveletBuffer;

    struct RawData {
        Halide::Runtime::Buffer<uint16_t> rawBuffer;
        Halide::Runtime::Buffer<uint8_t> previewBuffer;
        RawImageMetadata metadata;
    };

    struct HdrMetadata {
        float exposureScale;
        float error;
        Halide::Runtime::Buffer<uint16_t> hdrInput;
        Halide::Runtime::Buffer<uint8_t> mask;
    };

    struct PreviewMetadata {
        std::vector<cv::Rect> faces;
        
        PreviewMetadata(const std::string& metadata) {
            std::string err;
            json11::Json metadataJson = json11::Json::parse(metadata, err);

            if(!err.empty()) {
                return;
            }

            auto facesIt = metadataJson.object_items().find("faces");
            if(facesIt != metadataJson.object_items().end()) {
                auto facesArray = facesIt->second.array_items();
                for(auto& f : facesArray) {
                    auto data = f.object_items();
                    
                    int left = data["left"].int_value();
                    int top = data["top"].int_value();
                    int right = data["right"].int_value();
                    int bottom = data["bottom"].int_value();
                    
                    auto boundingBox = cv::Rect(left, top, right - left, bottom - top);
                    
                    faces.push_back(boundingBox);
                }
            }
        }
    };

    // https://exiv2.org/doc/geotag_8cpp-example.html
    static std::string toExifString(double d, bool isRational, bool isLatitude)
    {
        const char* NS   = d>=0.0?"N":"S";
        const char* EW   = d>=0.0?"E":"W";
        const char* NSEW = isLatitude  ? NS: EW;
        
        if ( d < 0 ) d = -d;
        
        int deg = (int) d;
            d  -= deg;
            d  *= 60;
        
        int min = (int) d ;
            d  -= min;
            d  *= 60;
        
        int sec = (int)d;
        
        char result[200];
        
        if (isRational)
            sprintf(result,"%d/1 %d/1 %d/1",deg, min, sec);
        else
            sprintf(result,"%03d%s%02d'%02d\"%s", deg, "deg", min, sec, NSEW);
        
        return std::string(result);
    }

    static std::string toExifString(double d)
    {
        char result[200];
        d *= 100;
        sprintf(result,"%d/100",abs((int)d));
        
        return std::string(result);
    }

    template<typename T>


    struct NativeBufferContext {
        NativeBufferContext(NativeBuffer& buffer, bool write) : nativeBuffer(buffer)
        {
            nativeBufferData = nativeBuffer.lock(write);
        }
        
        Halide::Runtime::Buffer<uint8_t> getHalideBuffer() {
            return Halide::Runtime::Buffer<uint8_t>(nativeBufferData, (int) nativeBuffer.len(), 1);
        }
        
        ~NativeBufferContext() {
            nativeBuffer.unlock();
        }
        
    private:
        NativeBuffer& nativeBuffer;
        uint8_t* nativeBufferData;
    };

    ImageProgressHelper::ImageProgressHelper(const ImageProcessorProgress& progressListener, int numImages, int start) :
        mStart(start), mProgressListener(progressListener), mNumImages(numImages), mCurImage(0)
    {
        // Per fused image increment is numImages over a 75% progress amount.
        mPerImageIncrement = 75.0 / numImages;
    }
    
    void ImageProgressHelper::postProcessCompleted() {
        mProgressListener.onProgressUpdate(mStart + 95);
    }
    
    void ImageProgressHelper::denoiseCompleted() {
        // Starting point is mStart, denoising takes 50%, progress should now be mStart + 50%
        mProgressListener.onProgressUpdate(mStart + 75);
    }

    void ImageProgressHelper::nextFusedImage() {
        ++mCurImage;
        mProgressListener.onProgressUpdate(static_cast<int>(mStart + (mPerImageIncrement * mCurImage)));
    }

    void ImageProgressHelper::imageSaved() {
        mProgressListener.onProgressUpdate(100);
        mProgressListener.onCompleted();
    }

    double ImageProcessor::calcEv(const RawCameraMetadata& cameraMetadata, const RawImageMetadata& metadata) {
        double a = 1.8;
        if(!cameraMetadata.apertures.empty())
            a = cameraMetadata.apertures[0];

        double s = a*a;
        return std::log2(s / (metadata.exposureTime / (1.0e9))) - std::log2(metadata.iso / 100.0);
    }


    cv::Mat ImageProcessor::postProcess(std::vector<Halide::Runtime::Buffer<uint16_t>>& inputBuffers,
                                        const shared_ptr<HdrMetadata>& hdrMetadata,
                                        int offsetX,
                                        int offsetY,
                                        const float chromaEps,
                                        const RawImageMetadata& metadata,
                                        const RawCameraMetadata& cameraMetadata,
                                        const PostProcessSettings& settings)
    {
    return cv::Mat();
}

    float ImageProcessor::estimateShadows(const cv::Mat& histogram, float keyValue) {
        if (histogram.empty() || histogram.cols <= 0 || histogram.rows <= 0) {
            return 1.0f;
        }

        float avgLuminance = 0.0f;
        
        float totalPixels = 0;
        
        int lowerBound = 0;
        int upperBound = histogram.cols;
        
        for(int i = lowerBound; i < upperBound; i++) {
            avgLuminance += histogram.at<float>(i) * log(1e-5 + i / 255.0f);
            totalPixels += histogram.at<float>(i);
        }
        
        avgLuminance = exp(avgLuminance / (totalPixels + 1e-5f));
        
        return std::max(1.0f, std::min(keyValue / avgLuminance, 16.0f));
    }

    float ImageProcessor::estimateExposureCompensation(const cv::Mat& histogram, float threshold) {
        if (histogram.empty() || histogram.cols <= 0 || histogram.rows <= 0) {
            return 0.0f;
        }

        int bin = 0;
        float total = 0.0f;
        
        // Exposure compensation
        for(int i = histogram.cols - 1; i >= 0; i--) {
            total += histogram.at<float>(i);
            if(total >= threshold) {
                bin = i;
                break;
            }
        }
        
        // Estimate underexpose if overexposed
        if(bin >= histogram.cols - 2) {
            for(int i = histogram.cols - 1; i >= (int) (0.75f*histogram.cols); i--) {
                total += histogram.at<float>(i);
            }

            float p = std::log2(150*total);
                        
            return std::min(0.0f, std::max(-3.0f, -p));
        }
        
        double m = histogram.cols / static_cast<double>(bin + 1);
        return std::log2(m);
    }

    float ImageProcessor::estimateChromaEps(const RawImageBuffer& rawBuffer,
                                            const RawCameraMetadata& cameraMetadata)
    {
        PostProcessSettings settings;
        
        settings.shadows = 1.0f;
        settings.blacks = 0;
        settings.contrast = 0.0f;
        settings.sharpen0 = 0;
        settings.sharpen1 = 0;
        
        auto previewBuffer = createPreview(rawBuffer, 4, cameraMetadata, settings);
        cv::Mat preview(previewBuffer.height(), previewBuffer.width(), CV_8UC4, previewBuffer.data());
        
        cv::cvtColor(preview, preview, cv::COLOR_BGRA2GRAY);
        
        preview.convertTo(preview, CV_32F, 1.0/255.0);
        cv::log(preview + 1.0/255.0, preview);
        
        float mean = exp(cv::mean(preview)[0]);
        
        auto ev = calcEv(cameraMetadata, rawBuffer.metadata);
        float w = -ev / 16 + 2;
        
        return std::fminf(0.2f, w * (0.01f + std::exp(-24.0f*mean)));
    }

    cv::Mat ImageProcessor::estimateBlacks(const RawImageBuffer& rawBuffer,
                                           const RawCameraMetadata& cameraMetadata,
                                           float shadows,
                                           float& outBlacks) {
        
        PostProcessSettings settings;
        
        settings.shadows = shadows;
        settings.blacks = 0;
        settings.contrast = 0.5f;
        settings.sharpen0 = 1;
        settings.sharpen1 = 1;
        settings.pop      = 1;
        
        auto previewBuffer = createPreview(rawBuffer, 4, cameraMetadata, settings);
        
        cv::Mat preview(previewBuffer.height(), previewBuffer.width(), CV_8UC4, previewBuffer.data());
        cv::Mat histogram;
        
        cv::cvtColor(preview, preview, cv::COLOR_BGRA2GRAY);
        
        vector<cv::Mat> inputImages     = { preview };
        const vector<int> channels      = { 0 };
        const vector<int> histBins      = { 255 };
        const vector<float> histRange   = { 0, 256 };

        cv::calcHist(inputImages, channels, cv::Mat(), histogram, histBins, histRange);
        
        histogram = histogram / (preview.rows * preview.cols);
                
        // Cumulative histogram
        for(int i = 1; i < histogram.rows; i++) {
            histogram.at<float>(i) += histogram.at<float>(i - 1);
        }
                
        // Estimate blacks
        const float minDehazePercent = 0.0005f;
        const float maxDehazePercent = 0.007f;
        const float binPercent = 0.06f;
        
        const int maxBin = 0.5f + (binPercent * histogram.rows);
        int endBin = 0;
        
        while(true) {
            float p = histogram.at<float>(endBin);
            
            if( p > maxDehazePercent || (p > minDehazePercent && endBin >= maxBin) )
                break;
            
            ++endBin;
        }

        outBlacks = std::max(0, endBin - 1) / (float)(histogram.rows - 1);

        return preview;
    }

    cv::Mat ImageProcessor::estimateWhitePoint(const RawImageBuffer& rawBuffer,
                                               const RawCameraMetadata& cameraMetadata,
                                               float shadows,
                                               float blacks,
                                               float threshold,
                                               float& outWhitePoint) {
        PostProcessSettings settings;
        
        settings.shadows = shadows;
        settings.blacks = blacks;
        settings.contrast = 0.5f;
        settings.sharpen0 = 1;
        settings.sharpen1 = 1;

        auto previewBuffer = createPreview(rawBuffer, 4, cameraMetadata, settings);
        
        cv::Mat preview(previewBuffer.height(), previewBuffer.width(), CV_8UC4, previewBuffer.data());
        cv::Mat histogram;
        
        cv::cvtColor(preview, preview, cv::COLOR_BGRA2GRAY);
        
        vector<cv::Mat> inputImages     = { preview };
        const vector<int> channels      = { 0 };
        const vector<int> histBins      = { 255 };
        const vector<float> histRange   = { 0, 256 };

        cv::calcHist(inputImages, channels, cv::Mat(), histogram, histBins, histRange);
        
        histogram = histogram / (preview.rows * preview.cols);

        // Cumulative histogram
        for(int i = 1; i < histogram.rows; i++) {
            histogram.at<float>(i) += histogram.at<float>(i - 1);
        }
                
        // Estimate white point
        int endBin = 0;
        for(endBin = histogram.rows - 1; endBin >= 192; endBin--) {
            if(histogram.at<float>(endBin) < threshold)
                break;
        }

        outWhitePoint = static_cast<float>(endBin + 1) / ((float) histogram.rows);
        
        return preview;
    }

    float ImageProcessor::getShadowKeyValue(const RawImageBuffer& rawBuffer, const RawCameraMetadata& cameraMetadata, bool nightMode) {
        float ev = calcEv(cameraMetadata, rawBuffer.metadata);
        float minKv = 1.07;
        
        if(nightMode)
            minKv = 1.12f;
        
        return minKv - SHADOW_BIAS / (SHADOW_BIAS + std::log10(std::pow(10.0f, ev) + 1));
    }

    void ImageProcessor::estimateBasicSettings(const RawImageBuffer& rawBuffer,
                                               const RawCameraMetadata& cameraMetadata,
                                               PostProcessSettings& outSettings)
    {
        // Safe default non-zero values
        outSettings.temperature    = 5000.0f;
        outSettings.tint           = 0.0f;
        outSettings.shadows        = 1.0f;
        outSettings.exposure       = 0.0f;
        outSettings.blacks         = 0.0f;
        outSettings.whitePoint     = 1.0f;
        outSettings.contrast       = 0.5f;
        outSettings.saturation     = 1.0f;
        outSettings.sceneLuminance = 1.0f;
        outSettings.noiseSigma     = 1.0f;

        try {
            cv::Vec3f asShot = rawBuffer.metadata.asShot;
            if (asShot[0] > 0.0f || asShot[1] > 0.0f || asShot[2] > 0.0f) {
                CameraProfile cameraProfile(cameraMetadata, rawBuffer.metadata);
                Temperature temperature;
                cameraProfile.temperatureFromVector(asShot, temperature);
                outSettings.temperature = static_cast<float>(temperature.temperature());
                outSettings.tint        = static_cast<float>(temperature.tint());
            }
        } catch (...) {
            // Keep safe defaults if temperature extraction fails
        }
    }

    void ImageProcessor::estimateWhiteBalance(const RawImageBuffer& rawBuffer,
                                              const RawCameraMetadata& cameraMetadata,
                                              float& outR,
                                              float& outG,
                                              float& outB)
    {
        outR = 1.0f;
        outG = 1.0f;
        outB = 1.0f;
    }

    void ImageProcessor::estimateSettings(const RawImageBuffer& rawBuffer,
                                          const RawCameraMetadata& cameraMetadata,
                                          PostProcessSettings& outSettings)
    {
        estimateBasicSettings(rawBuffer, cameraMetadata, outSettings);
    }

    void ImageProcessor::createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                          const RawImageMetadata& rawImageMetadata,
                                          const Temperature& temperature,
                                          cv::Vec3f& cameraWhite,
                                          cv::Mat& outCameraToPcs,
                                          cv::Mat& outPcsToSrgb)
    {
        cv::Mat pcsToCamera, cameraToPcs;
        cv::Mat pcsToSrgb, srgbToPcs;
        
        CameraProfile cameraProfile(cameraMetadata, rawImageMetadata);

        cameraProfile.cameraToPcs(temperature, pcsToCamera, cameraToPcs, cameraWhite);
        motioncam::CameraProfile::pcsToSrgb(pcsToSrgb, srgbToPcs);

        cameraToPcs.copyTo(outCameraToPcs);
        pcsToSrgb.copyTo(outPcsToSrgb);
    }

    void ImageProcessor::createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                          const RawImageMetadata& rawImageMetadata,
                                          const cv::Vec3f& asShot,
                                          cv::Vec3f& cameraWhite,
                                          cv::Mat& outCameraToPcs,
                                          cv::Mat& outPcsToSrgb)
    {
        cv::Mat pcsToCamera, cameraToPcs;
        cv::Mat pcsToSrgb, srgbToPcs;
        
        CameraProfile cameraProfile(cameraMetadata, rawImageMetadata);
        Temperature temperature;

        cv::Vec3f asShotVector = asShot;
        float max = math::max(asShotVector);
        
        if(max > 0) {
            asShotVector[0] = asShotVector[0] * (1.0f / max);
            asShotVector[1] = asShotVector[1] * (1.0f / max);
            asShotVector[2] = asShotVector[2] * (1.0f / max);
        }
        else {
            throw InvalidState("Camera white balance vector is zero");
        }

        cameraProfile.temperatureFromVector(asShotVector, temperature);

        cameraProfile.cameraToPcs(temperature, pcsToCamera, cameraToPcs, cameraWhite);
        motioncam::CameraProfile::pcsToSrgb(pcsToSrgb, srgbToPcs);

        cameraToPcs.copyTo(outCameraToPcs);
        pcsToSrgb.copyTo(outPcsToSrgb);
    }

    Halide::Runtime::Buffer<uint8_t> ImageProcessor::createPreview(const RawImageBuffer& rawBuffer,
                                                                   const int downscaleFactor,
                                                                   const RawCameraMetadata& cameraMetadata,
                                                                   const PostProcessSettings& settings)
    {
        int factor = std::max(1, downscaleFactor);
        int w = std::max(1, rawBuffer.width / 2 / factor);
        int h = std::max(1, rawBuffer.height / 2 / factor);
        Halide::Runtime::Buffer<uint8_t> preview(w, h, 4);
        preview.fill(16);
        return preview;
    }
    
    std::shared_ptr<RawData> ImageProcessor::loadRawImage(const RawImageBuffer& rawBuffer,
                                                          const RawCameraMetadata& cameraMetadata,
                                                          const bool extendEdges,
                                                          const float scalePreview)
    {
        auto data = std::make_shared<RawData>();
        int w = std::max(1, rawBuffer.width);
        int h = std::max(1, rawBuffer.height);
        data->rawBuffer = Halide::Runtime::Buffer<uint16_t>(w, h, 1);
        data->rawBuffer.fill(0);
        return data;
    }

    void ImageProcessor::measureImage(RawImageBuffer& rawBuffer, const RawCameraMetadata& cameraMetadata, float& outSceneLuminosity)
    {
        outSceneLuminosity = 1.0f;
    }

    cv::Mat ImageProcessor::registerImage2(
        const Halide::Runtime::Buffer<uint8_t>& referenceBuffer, const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer)
    {
        return cv::Mat();
    }

    cv::Mat ImageProcessor::registerImage(
        const Halide::Runtime::Buffer<uint8_t>& referenceBuffer, const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer)
    {
        return cv::Mat();
    }

    cv::Mat ImageProcessor::calcHistogram(const RawCameraMetadata& cameraMetadata,
                                          const RawImageBuffer& buffer,
                                          const bool cumulative,
                                          const int downscale)
    {
        return cv::Mat::zeros(1, 256, CV_32F);
    }

    void ImageProcessor::matchExposures(
        const RawCameraMetadata& cameraMetadata, const RawImageBuffer& reference, const RawImageBuffer& toMatch, float& outScale, float& outWhitePoint)
    {
        auto refHistogram = calcHistogram(cameraMetadata, reference, true, 4);
        auto toMatchHistogram = calcHistogram(cameraMetadata, toMatch, true, 4);
        
        std::vector<float> matches;
        
        for(int i = 0; i < toMatchHistogram.cols; i++) {
            float a = toMatchHistogram.at<float>(i);

            for(int j = 1; j < refHistogram.cols; j++) {
                float b = refHistogram.at<float>(j);

                if(a <= b) {
                    double match = j / (i + 1.0);
                    matches.push_back(match);
                    break;
                }
            }
        }
        
        outWhitePoint = 1.0f;
        
        for(int i = toMatchHistogram.cols - 1; i >= 0; i--) {
            float a = toMatchHistogram.at<float>(i);
            if(a < WHITEPOINT_THRESHOLD) {
                outWhitePoint = toMatchHistogram.cols / (float) (i + 1);
                break;
            }
                
        }
        
        float scale = 0;
        
        int Imin = std::min((int) matches.size(), 4);
        int Imax = std::min((int) matches.size(), 32);
        
        for(int i = Imin; i < Imax; i++) {
            scale += matches[i];
        }
        
        if(Imax - Imin <= 0)
            outScale = 1.0f;
        else
            outScale = scale / (float) (Imax - Imin);
    }

    void ImageProcessor::process(RawContainer& rawContainer, const std::string& outputPath, const ImageProcessorProgress& progressListener)
    {
        // If this is a HDR capture then find the underexposed images.
        std::vector<std::shared_ptr<RawImageBuffer>> underexposedImages;
        
        // Started
        progressListener.onProgressUpdate(0);
        
        //
        // Pick sharpest image when shooting in night mode
        //
        
        if(rawContainer.getPostProcessSettings().captureMode == "NIGHT") {
            logger::log("Selecting sharpest image");
            
            std::map<std::string, float> sharpness;
            
            for(auto& p : rawContainer.getFrames()) {
                auto s = rawContainer.loadFrame(p);
                sharpness[p] = measureSharpness(*s);
            }
            
            rawContainer.updateReferenceImage(sharpness.rbegin()->first);
        }

        if(rawContainer.isHdr()) {
            double maxEv = -1e10;
            double minEv = 1e10;
            
            // Figure out where the base & underexposed images are
            for(auto frameName : rawContainer.getFrames()) {
                auto frame = rawContainer.getFrame(frameName);
                auto ev = calcEv(rawContainer.getCameraMetadata(), frame->metadata);
                
                if(ev > maxEv)
                    maxEv = ev;
                
                if(ev < minEv)
                    minEv = ev;
            }
                        
            // Make sure there's enough of a difference between the base and underexposed images
            if(std::abs(maxEv - minEv) > 0.49) {
                for(auto frameName : rawContainer.getFrames()) {
                    auto frame = rawContainer.getFrame(frameName);
                    auto ev = calcEv(rawContainer.getCameraMetadata(), frame->metadata);
                
                    if(std::abs(ev - maxEv) < std::abs(ev - minEv)) {
                        // Load the frame since we intend to remove it from the container
                        auto raw = rawContainer.loadFrame(frameName);
                        underexposedImages.push_back(raw);
                        
                        rawContainer.removeFrame(frameName);
                    }
                }
            }
        }
        
        auto referenceRawBuffer = rawContainer.loadFrame(rawContainer.getReferenceImage());
        PostProcessSettings settings = rawContainer.getPostProcessSettings();
        
        // Estimate shadows if not set
        if(settings.shadows < 0) {
            float keyValue = getShadowKeyValue(*referenceRawBuffer, rawContainer.getCameraMetadata(), settings.captureMode == "NIGHT");
            cv::Mat histogram = calcHistogram(rawContainer.getCameraMetadata(), *referenceRawBuffer, false, 4);
            
            settings.shadows = estimateShadows(histogram, keyValue);
        }

        // Estimate black point of not provided
        if(settings.blacks < 0) {
            estimateBlacks(*referenceRawBuffer,
                           rawContainer.getCameraMetadata(),
                           settings.shadows,
                           settings.blacks);
        }
        
        // Estimate white point if not supplied
        if(settings.whitePoint < 0) {
            estimateWhitePoint(*referenceRawBuffer,
                               rawContainer.getCameraMetadata(),
                               settings.shadows,
                               settings.blacks,
                               WHITEPOINT_THRESHOLD,
                               settings.whitePoint);
        }
        
        //
        // Save preview
        //
        
        auto preview = createPreview(*referenceRawBuffer, 2, rawContainer.getCameraMetadata(), settings);
        std::string basePath, filename;

        cv::Mat previewImage(preview.height(), preview.width(), CV_8UC4, preview.data());
                
        util::GetBasePath(outputPath, basePath, filename);
        std::string previewPath = basePath + "/PREVIEW_" + filename;
        
        cv::cvtColor(previewImage, previewImage, cv::COLOR_RGBA2BGR);
        cv::imwrite(previewPath, previewImage);
        
        // Parse the returned metadata
        std::string metadataJson = progressListener.onPreviewSaved(previewPath);
        PreviewMetadata previewMetadata(metadataJson);

//        // Adjust shadows to lighten any faces in the image
//        float shadowsScale = adjustShadowsForFaces(previewImage, previewMetadata);
//        logger::log("Adjusting shadows by " + std::to_string(shadowsScale));
//
//        settings.shadows *= shadowsScale;

        previewImage.release();

        //
        // HDR
        //
        
        shared_ptr<HdrMetadata> hdrMetadata;
        shared_ptr<RawImageBuffer> underExposedImage;
        
        if(!underexposedImages.empty()) {
            auto referenceRawBuffer = rawContainer.loadFrame(rawContainer.getReferenceImage());
            auto underexposedFrameIt = underexposedImages.begin();

            auto hist = calcHistogram(rawContainer.getCameraMetadata(), *referenceRawBuffer, false, 4);
            const int bound = (int) (hist.cols * 0.95f);
            float sum = 0;

            for(int x = hist.cols - 1; x >= bound; x--) {
                sum += hist.at<float>(x);
            }

            // Check if there's any point even using the underexposed image (less than 0.1% in the >95% bins)
            float p = sum * 100.0f;
            if(p < 0.09f) {
                logger::log("Skipping HDR processing (" + std::to_string(p) + ")");
            }
            // Try each underexposed image
            else {
                logger::log("Using HDR processing (" + std::to_string(p) + ")");
                
                while(underexposedFrameIt != underexposedImages.end()) {
                    hdrMetadata =
                        prepareHdr(rawContainer.getCameraMetadata(),
                                   settings,
                                   *referenceRawBuffer,
                                   *(*underexposedFrameIt));

                    if(hdrMetadata) {
                        underExposedImage = *underexposedFrameIt;
                        break;
                    }

                    ++underexposedFrameIt;
                }
            }
        }
                
        //
        // Denoise
        //
        
        ImageProgressHelper progressHelper(progressListener, static_cast<int>(rawContainer.getFrames().size()), 0);
        
        std::vector<Halide::Runtime::Buffer<uint16_t>> denoiseOutput;
        denoiseOutput = denoise(rawContainer, progressHelper);
        
        progressHelper.denoiseCompleted();
        
        //
        // Post process
        //
        
        const int rawWidth  = referenceRawBuffer->width / 2;
        const int rawHeight = referenceRawBuffer->height / 2;

        const int T = pow(2, DENOISE_LEVELS);
        
        const int offsetX = static_cast<int>(T * ceil(rawWidth / (double) T) - rawWidth);
        const int offsetY = static_cast<int>(T * ceil(rawHeight / (double) T) - rawHeight);

        // Check if we should write a DNG file
#ifdef DNG_SUPPORT
        if(rawContainer.getPostProcessSettings().dng) {
            std::vector<cv::Mat> rawChannels;
            rawChannels.reserve(4);

            for(int i = 0; i < 4; i++) {
                rawChannels.emplace_back(denoiseOutput[i].height(), denoiseOutput[i].width(), CV_16U, denoiseOutput[i].data());
            }

            switch(rawContainer.getCameraMetadata().sensorArrangment) {
                default:
                case ColorFilterArrangment::RGGB:
                    // All good
                    break;

                case ColorFilterArrangment::GRBG:
                {
                    std::vector<cv::Mat> tmp = rawChannels;
                    
                    rawChannels[0] = tmp[1];
                    rawChannels[1] = tmp[0];
                    rawChannels[2] = tmp[3];
                    rawChannels[3] = tmp[2];
                }
                break;

                case ColorFilterArrangment::GBRG:
                {
                    std::vector<cv::Mat> tmp = rawChannels;
                    
                    rawChannels[0] = tmp[2];
                    rawChannels[1] = tmp[0];
                    rawChannels[2] = tmp[3];
                    rawChannels[3] = tmp[1];
                }
                break;

                case ColorFilterArrangment::BGGR:
                    std::swap(rawChannels[0], rawChannels[3]);
                    break;
            }

            cv::Mat rawImage = buildRawImage(rawChannels, offsetX, offsetY);
            
            size_t extensionStartIdx = outputPath.find_last_of('.');
            std::string rawOutputPath;
            
            if(extensionStartIdx != std::string::npos) {
                rawOutputPath = outputPath.substr(0, extensionStartIdx);
            }
            else {
                rawOutputPath = outputPath;
            }
            
            writeDng(rawImage, rawContainer.getCameraMetadata(), referenceRawBuffer->metadata, rawOutputPath + ".dng");
        }
#endif
                           
        float chromaEps = estimateChromaEps(*referenceRawBuffer, rawContainer.getCameraMetadata());
        
        logger::log("Estimated chroma eps " + std::to_string(chromaEps));
        
        cv::Mat outputImage = postProcess(
            denoiseOutput,
            hdrMetadata,
            offsetX,
            offsetY,
            chromaEps,
            referenceRawBuffer->metadata,
            rawContainer.getCameraMetadata(),
            settings);
        
        progressHelper.postProcessCompleted();

        // Write image
        std::vector<int> writeParams = { cv::IMWRITE_JPEG_QUALITY, rawContainer.getPostProcessSettings().jpegQuality };
        cv::imwrite(outputPath, outputImage, writeParams);

        // Create thumbnail
        cv::Mat thumbnail;

        int width = 320;
        int height = (int) std::lround((outputImage.rows / (double) outputImage.cols) * width);

        cv::resize(outputImage, thumbnail, cv::Size(width, height));

        // Add exif data to the output image
        addExifMetadata(underExposedImage == nullptr ? referenceRawBuffer->metadata : underExposedImage->metadata,
                        thumbnail,
                        rawContainer.getCameraMetadata(),
                        rawContainer.getPostProcessSettings(),
                        outputPath);
        
        progressHelper.imageSaved();
    }

    void ImageProcessor::process(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ImageProcessorProgress& progressListener)
    {
        Measure measure("process()");

        // Open RAW container
        RawContainer rawContainer(inputPath);

        if(rawContainer.getFrames().empty()) {
            progressListener.onError("No frames found");
            return;
        }
        
        process(rawContainer, outputPath, progressListener);
    }

    float ImageProcessor::adjustShadowsForFaces(cv::Mat input, PreviewMetadata& metadata) {
        return 1.0f;
                
//        if(metadata.faces.empty())
//            return 1.0f;
//
//        cv::Mat gray;
//        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
//
//        float L = 0;
//        float n = 0;
//
//        for(auto f : metadata.faces) {
//            float p = f.area() / (input.cols*input.rows);
//            // Ignore faces which are too small
//            if(p >= 0.00083f) {
//                auto m = cv::mean(gray(f & cv::Rect(0, 0, input.cols, input.rows)));
//
//                L += m[0];
//                n += 1;
//            }
//        }
//
//        if(n <= 0)
//            return 1;
//
//        L = L / n;
//        return std::min(4.0f, std::max(1.0f, 128.0f / L));
    }
    
    void ImageProcessor::addExifMetadata(const RawImageMetadata& metadata,
                                         const cv::Mat& thumbnail,
                                         const RawCameraMetadata& cameraMetadata,
                                         const PostProcessSettings& settings,
                                         const std::string& inputOutput)
    {
        auto image = Exiv2::ImageFactory::open(inputOutput);
        if(image.get() == nullptr)
            return;
        
        image->readMetadata();
        
        Exiv2::ExifData& exifData = image->exifData();
        
        // sRGB color space
        exifData["Exif.Photo.ColorSpace"]       = uint16_t(1);
        
        // Capture settings
        exifData["Exif.Photo.ISOSpeedRatings"]  = uint16_t(metadata.iso);
        exifData["Exif.Photo.ExposureTime"]     = Exiv2::floatToRationalCast(metadata.exposureTime / ((float) 1e9));
        
        switch(metadata.screenOrientation)
        {
            default:
            case ScreenOrientation::LANDSCAPE:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(2) : uint16_t(1);
                break;
                
            case ScreenOrientation::PORTRAIT:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(5) : uint16_t(6);
                break;
                                
            case ScreenOrientation::REVERSE_LANDSCAPE:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(4) : uint16_t(3);
                break;
                
            case ScreenOrientation::REVERSE_PORTRAIT:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(7) : uint16_t(8);
                break;
        }
                
        if(!cameraMetadata.apertures.empty())
            exifData["Exif.Photo.ApertureValue"] = Exiv2::floatToRationalCast(cameraMetadata.apertures[0]);

        if(!cameraMetadata.focalLengths.empty())
            exifData["Exif.Photo.FocalLength"] = Exiv2::floatToRationalCast(cameraMetadata.focalLengths[0]);
        
        // Misc bits
        exifData["Exif.Photo.LensModel"]   = "MotionCam";
        exifData["Exif.Photo.LensMake"]    = "MotionCam";
        
        exifData["Exif.Photo.SceneType"]    = uint8_t(1);
        exifData["Exif.Image.XResolution"]  = Exiv2::Rational(72, 1);
        exifData["Exif.Image.YResolution"]  = Exiv2::Rational(72, 1);
        exifData["Exif.Photo.WhiteBalance"] = uint8_t(0);
        
        // Store GPS coords
        if(!settings.gpsTime.empty()) {
            exifData["Exif.GPSInfo.GPSProcessingMethod"]    = "65 83 67 73 73 0 0 0 72 89 66 82 73 68 45 70 73 88"; // ASCII HYBRID-FIX
            exifData["Exif.GPSInfo.GPSVersionID"]           = "2 2 0 0";
            exifData["Exif.GPSInfo.GPSMapDatum"]            = "WGS-84";
            
            exifData["Exif.GPSInfo.GPSLatitude"]            = toExifString(settings.gpsLatitude, true, true);
            exifData["Exif.GPSInfo.GPSLatitudeRef"]         = settings.gpsLatitude > 0 ? "N" : "S";

            exifData["Exif.GPSInfo.GPSLongitude"]           = toExifString(settings.gpsLongitude, true, false);
            exifData["Exif.GPSInfo.GPSLongitudeRef"]        = settings.gpsLongitude > 0 ? "E" : "W";
            
            exifData["Exif.GPSInfo.GPSAltitude"]            = toExifString(settings.gpsAltitude);
            exifData["Exif.GPSInfo.GPSAltitudeRef"]         = settings.gpsAltitude < 0.0 ? "1" : "0";
            
            exifData["Exif.Image.GPSTag"]                   = 4908;
        }
        
        // Set thumbnail
        if(!thumbnail.empty()) {
            Exiv2::ExifThumb exifThumb(exifData);
            std::vector<uint8_t> thumbnailBuffer;
            
            cv::imencode(".jpg", thumbnail, thumbnailBuffer);
            
            exifThumb.setJpegThumbnail(thumbnailBuffer.data(), thumbnailBuffer.size());
        }
        
        image->writeMetadata();
    }

    double ImageProcessor::measureSharpness(const RawImageBuffer& rawBuffer) {
    return 0.0;
}

    std::vector<Halide::Runtime::Buffer<uint16_t>> ImageProcessor::denoise(RawContainer& rawContainer, ImageProgressHelper& progressHelper)
    {
    return std::vector<Halide::Runtime::Buffer<uint16_t>>();
}

#ifdef DNG_SUPPORT
    cv::Mat ImageProcessor::buildRawImage(vector<cv::Mat> channels, int cropX, int cropY) {
        const uint32_t height = channels[0].rows * 2;
        const uint32_t width  = channels[1].cols * 2;
        
        cv::Mat outputImage(height, width, CV_16U);
        
        for (int y = 0; y < height; y+=2) {
            auto* outRow1 = outputImage.ptr<uint16_t>(y);
            auto* outRow2 = outputImage.ptr<uint16_t>(y + 1);
            
            int ry = y / 2;
            
            const uint16_t* inC0 = channels[0].ptr<uint16_t>(ry);
            const uint16_t* inC1 = channels[1].ptr<uint16_t>(ry);
            const uint16_t* inC2 = channels[2].ptr<uint16_t>(ry);
            const uint16_t* inC3 = channels[3].ptr<uint16_t>(ry);
            
            for(int x = 0; x < width; x+=2) {
                int rx = x / 2;
                
                outRow1[x]      = inC0[rx];
                outRow1[x + 1]  = inC1[rx];
                outRow2[x]      = inC2[rx];
                outRow2[x + 1]  = inC3[rx];
            }
        }
        
        return outputImage(cv::Rect(cropX, cropY, width - cropX*2, height - cropY*2)).clone();
    }

    void ImageProcessor::writeDng(cv::Mat& rawImage,
                                  const RawCameraMetadata& cameraMetadata,
                                  const RawImageMetadata& imageMetadata,
                                  const std::string& outputPath)
    {
        Measure measure("writeDng()");
        
        const int width  = rawImage.cols;
        const int height = rawImage.rows;
        
        dng_host host;

        host.SetSaveLinearDNG(false);
        host.SetSaveDNGVersion(dngVersion_SaveDefault);
        
        AutoPtr<dng_negative> negative(host.Make_dng_negative());
        
        // Create lens shading map for each channel
        for(int c = 0; c < 4; c++) {
            dng_point channelGainMapPoints(imageMetadata.lensShadingMap[c].rows, imageMetadata.lensShadingMap[c].cols);
            
            AutoPtr<dng_gain_map> gainMap(new dng_gain_map(host.Allocator(),
                                                           channelGainMapPoints,
                                                           dng_point_real64(1.0 / (imageMetadata.lensShadingMap[c].rows), 1.0 / (imageMetadata.lensShadingMap[c].cols)),
                                                           dng_point_real64(0, 0),
                                                           1));
            
            for(int y = 0; y < imageMetadata.lensShadingMap[c].rows; y++) {
                for(int x = 0; x < imageMetadata.lensShadingMap[c].cols; x++) {
                    gainMap->Entry(y, x, 0) = imageMetadata.lensShadingMap[c].at<float>(y, x);
                }
            }
            
            int left = 0;
            int top  = 0;
            
            if(c == 0) {
                left = 0;
                top = 0;
            }
            else if(c == 1) {
                left = 1;
                top = 0;
            }
            else if(c == 2) {
                left = 0;
                top = 1;
            }
            else if(c == 3) {
                left = 1;
                top = 1;
            }
            
            dng_rect gainMapArea(top, left, height, width);
            AutoPtr<dng_opcode> gainMapOpCode(new dng_opcode_GainMap(dng_area_spec(gainMapArea, 0, 1, 2, 2), gainMap));
            
            negative->OpcodeList2().Append(gainMapOpCode);
        }
        
        negative->SetModelName("MotionCam");
        negative->SetLocalName("MotionCam");
        
        // We always use RGGB at this point
        negative->SetColorKeys(colorKeyRed, colorKeyGreen, colorKeyBlue);
                
        negative->SetBayerMosaic(1);
        negative->SetColorChannels(3);
        
        negative->SetQuadBlacks(0, 0, 0, 0);
        negative->SetWhiteLevel(EXPANDED_RANGE);
        
        // Square pixels
        negative->SetDefaultScale(dng_urational(1,1), dng_urational(1,1));
        
        negative->SetDefaultCropSize(width, height);
        negative->SetNoiseReductionApplied(dng_urational(1,1));
        negative->SetCameraNeutral(dng_vector_3(imageMetadata.asShot[0], imageMetadata.asShot[1], imageMetadata.asShot[2]));

        dng_orientation orientation;
        
        switch(imageMetadata.screenOrientation)
        {
            default:
            case ScreenOrientation::PORTRAIT:
                orientation = dng_orientation::Rotate90CW();
                break;
            
            case ScreenOrientation::REVERSE_PORTRAIT:
                orientation = dng_orientation::Rotate90CCW();
                break;
                
            case ScreenOrientation::LANDSCAPE:
                orientation = dng_orientation::Normal();
                break;
                
            case ScreenOrientation::REVERSE_LANDSCAPE:
                orientation = dng_orientation::Rotate180();
                break;
        }
        
        negative->SetBaseOrientation(orientation);

        // Set up camera profile
        AutoPtr<dng_camera_profile> cameraProfile(new dng_camera_profile());
        
        // Color matrices
        cv::Mat color1 = cameraMetadata.colorMatrix1;
        cv::Mat color2 = cameraMetadata.colorMatrix2;
        
        dng_matrix_3by3 dngColor1 = dng_matrix_3by3(color1.at<float>(0, 0), color1.at<float>(0, 1), color1.at<float>(0, 2),
                                                    color1.at<float>(1, 0), color1.at<float>(1, 1), color1.at<float>(1, 2),
                                                    color1.at<float>(2, 0), color1.at<float>(2, 1), color1.at<float>(2, 2));
        
        dng_matrix_3by3 dngColor2 = dng_matrix_3by3(color2.at<float>(0, 0), color2.at<float>(0, 1), color2.at<float>(0, 2),
                                                    color2.at<float>(1, 0), color2.at<float>(1, 1), color2.at<float>(1, 2),
                                                    color2.at<float>(2, 0), color2.at<float>(2, 1), color2.at<float>(2, 2));
        
        cameraProfile->SetColorMatrix1(dngColor1);
        cameraProfile->SetColorMatrix2(dngColor2);
        
        // Forward matrices
        cv::Mat forward1 = cameraMetadata.forwardMatrix1;
        cv::Mat forward2 = cameraMetadata.forwardMatrix2;
        
        if(!forward1.empty() && !forward2.empty()) {
            dng_matrix_3by3 dngForward1 = dng_matrix_3by3( forward1.at<float>(0, 0), forward1.at<float>(0, 1), forward1.at<float>(0, 2),
                                                           forward1.at<float>(1, 0), forward1.at<float>(1, 1), forward1.at<float>(1, 2),
                                                           forward1.at<float>(2, 0), forward1.at<float>(2, 1), forward1.at<float>(2, 2) );
            
            dng_matrix_3by3 dngForward2 = dng_matrix_3by3( forward2.at<float>(0, 0), forward2.at<float>(0, 1), forward2.at<float>(0, 2),
                                                           forward2.at<float>(1, 0), forward2.at<float>(1, 1), forward2.at<float>(1, 2),
                                                           forward2.at<float>(2, 0), forward2.at<float>(2, 1), forward2.at<float>(2, 2) );
            
            cameraProfile->SetForwardMatrix1(dngForward1);
            cameraProfile->SetForwardMatrix2(dngForward2);
        }
        
        uint32_t illuminant1 = 0;
        uint32_t illuminant2 = 0;
        
        // Convert to DNG format
        switch(cameraMetadata.colorIlluminant1) {
            case color::StandardA:
                illuminant1 = lsStandardLightA;
                break;
            case color::StandardB:
                illuminant1 = lsStandardLightB;
                break;
            case color::StandardC:
                illuminant1 = lsStandardLightC;
                break;
            case color::D50:
                illuminant1 = lsD50;
                break;
            case color::D55:
                illuminant1 = lsD55;
                break;
            case color::D65:
                illuminant1 = lsD65;
                break;
            case color::D75:
                illuminant1 = lsD75;
                break;
        }
        
        switch(cameraMetadata.colorIlluminant2) {
            case color::StandardA:
                illuminant2 = lsStandardLightA;
                break;
            case color::StandardB:
                illuminant2 = lsStandardLightB;
                break;
            case color::StandardC:
                illuminant2 = lsStandardLightC;
                break;
            case color::D50:
                illuminant2 = lsD50;
                break;
            case color::D55:
                illuminant2 = lsD55;
                break;
            case color::D65:
                illuminant2 = lsD65;
                break;
            case color::D75:
                illuminant2 = lsD75;
                break;
        }
        
        cameraProfile->SetCalibrationIlluminant1(illuminant1);
        cameraProfile->SetCalibrationIlluminant2(illuminant2);
        
        cameraProfile->SetName("MotionCam");
        cameraProfile->SetEmbedPolicy(pepAllowCopying);
        
        // This ensures profile is saved
        cameraProfile->SetWasReadFromDNG();
        
        negative->AddProfile(cameraProfile);
        
        // Finally add the raw data to the negative
        dng_rect dngArea(height, width);
        dng_pixel_buffer dngBuffer;

        AutoPtr<dng_image> dngImage(host.Make_dng_image(dngArea, 1, ttShort));

        dngBuffer.fArea         = dngArea;
        dngBuffer.fPlane        = 0;
        dngBuffer.fPlanes       = 1;
        dngBuffer.fRowStep      = dngBuffer.fPlanes * width;
        dngBuffer.fColStep      = dngBuffer.fPlanes;
        dngBuffer.fPixelType    = ttShort;
        dngBuffer.fPixelSize    = TagTypeSize(ttShort);
        dngBuffer.fData         = rawImage.ptr();
        
        dngImage->Put(dngBuffer);
        
        // Build the DNG images
        negative->SetStage1Image(dngImage);
        negative->BuildStage2Image(host);
        negative->BuildStage3Image(host);
        
        negative->SynchronizeMetadata();
        
        // Create stream writer for output file
        dng_file_stream dngStream(outputPath.c_str(), true);
        
        // Write DNG file to disk
        AutoPtr<dng_image_writer> dngWriter(new dng_image_writer());
        
        dngWriter->WriteDNG(host, dngStream, *negative.Get(), nullptr, ccUncompressed);
    }
#endif // DNG_SUPPORT

    std::shared_ptr<HdrMetadata> ImageProcessor::prepareHdr(const RawCameraMetadata& cameraMetadata,
                                                            const PostProcessSettings& settings,
                                                            const RawImageBuffer& reference,
                                                            const RawImageBuffer& underexposed)
    {
    return std::make_shared<HdrMetadata>();
}
}
