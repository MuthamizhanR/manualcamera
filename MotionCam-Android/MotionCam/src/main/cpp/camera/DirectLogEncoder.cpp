#include "DirectLogEncoder.h"
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <dlfcn.h>
#include <media/NdkMediaCodec.h>

// AMediaCodec_createInputSurface / signalEndOfInputStream are API 26+, but minSdk is 24.
// Resolve them at runtime to keep the build and older devices working.
static media_status_t DL_AMediaCodec_createInputSurface(AMediaCodec* codec, ANativeWindow** surface) {
    using Fn = media_status_t (*)(AMediaCodec*, ANativeWindow**);
    static Fn fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "AMediaCodec_createInputSurface"));
    return fn ? fn(codec, surface) : AMEDIA_ERROR_UNSUPPORTED;
}

static media_status_t DL_AMediaCodec_signalEndOfInputStream(AMediaCodec* codec) {
    using Fn = media_status_t (*)(AMediaCodec*);
    static Fn fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "AMediaCodec_signalEndOfInputStream"));
    return fn ? fn(codec) : AMEDIA_ERROR_UNSUPPORTED;
}

#define LOG_TAG "DirectLogEncoder"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Constants for 10-bit color formats
#define COLOR_FormatSurface 0x7F000789
#define COLOR_Standard_BT2020 6
#define COLOR_Transfer_HLG 7 // or ST2084 (6) for HDR10

namespace motioncam {

DirectLogEncoder::DirectLogEncoder()
    : mVideoCodec(nullptr)
    , mAudioCodec(nullptr)
    , mMuxer(nullptr)
    , mInputSurface(nullptr)
    , mEglDisplay(EGL_NO_DISPLAY)
    , mEglContext(EGL_NO_CONTEXT)
    , mEglSurface(EGL_NO_SURFACE)
    , mVideoTrackIndex(-1)
    , mAudioTrackIndex(-1)
    , mIsRunning(false)
    , mMuxerStarted(false)
    , mHasVideoEos(false)
    , mHasAudioEos(false) {
}

DirectLogEncoder::~DirectLogEncoder() {
    release();
}

bool DirectLogEncoder::configure(const std::string& videoCodecMime, int width, int height, int bitrate, int fps, const std::string& outputFilePath) {
    std::lock_guard<std::mutex> lock(mMutex);

    mVideoCodecMime = videoCodecMime;
    mWidth = width;
    mHeight = height;
    mBitrate = bitrate;
    mFps = fps;
    mOutputFilePath = outputFilePath;

    if (!setupVideoEncoder()) {
        LOGE("Failed to setup video encoder");
        return false;
    }

    if (!setupEGL()) {
        LOGE("Failed to setup EGL");
        return false;
    }

    if (!setupAudioEncoder()) {
        LOGE("Failed to setup audio encoder");
        return false;
    }

    int fd = open(mOutputFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        LOGE("Failed to open output file: %s", mOutputFilePath.c_str());
        return false;
    }

    mMuxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    close(fd);

    if (!mMuxer) {
        LOGE("Failed to create muxer");
        return false;
    }

    return true;
}

bool DirectLogEncoder::setupVideoEncoder() {
    mVideoCodec = AMediaCodec_createEncoderByType(mVideoCodecMime.c_str());
    if (!mVideoCodec) {
        LOGE("Failed to create video encoder for mime type: %s", mVideoCodecMime.c_str());
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mVideoCodecMime.c_str());
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, mWidth);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, mHeight);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, mBitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, mFps);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);

    // Set 10-bit format
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatSurface);
    AMediaFormat_setInt32(format, "color-standard", COLOR_Standard_BT2020);
    AMediaFormat_setInt32(format, "color-transfer", COLOR_Transfer_HLG);

    // Profile Main10 for HEVC
    if (mVideoCodecMime == "video/hevc") {
        AMediaFormat_setInt32(format, "profile", 2); // HEVCProfileMain10
        AMediaFormat_setInt32(format, "level", 4096); // HEVCHighTierLevel51
    } else if (mVideoCodecMime == "video/av01") {
        AMediaFormat_setInt32(format, "profile", 2); // AV1ProfileMain10
        AMediaFormat_setInt32(format, "level", 8192); // AV1Level51
    }

    media_status_t status = AMediaCodec_configure(mVideoCodec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        LOGE("Failed to configure video encoder: %d", status);
        return false;
    }

    status = DL_AMediaCodec_createInputSurface(mVideoCodec, &mInputSurface);
    if (status != AMEDIA_OK || !mInputSurface) {
        LOGE("Failed to create input surface: %d", status);
        return false;
    }

    return true;
}

bool DirectLogEncoder::setupEGL() {
    if (!mInputSurface) {
        LOGE("No input surface available for EGL setup");
        return false;
    }

    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mEglDisplay == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }

    if (!eglInitialize(mEglDisplay, nullptr, nullptr)) {
        LOGE("Failed to initialize EGL");
        return false;
    }

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 10,
        EGL_GREEN_SIZE, 10,
        EGL_BLUE_SIZE, 10,
        EGL_ALPHA_SIZE, 2,
        EGL_RECORDABLE_ANDROID, EGL_TRUE,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(mEglDisplay, attribs, &config, 1, &numConfigs) || numConfigs == 0) {
        LOGE("Failed to choose EGL config for 10-bit recording, trying 8-bit fallback");
        const EGLint fallbackAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_RECORDABLE_ANDROID, EGL_TRUE,
            EGL_NONE
        };
        if (!eglChooseConfig(mEglDisplay, fallbackAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
            LOGE("Failed to choose fallback EGL config");
            return false;
        }
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    mEglContext = eglCreateContext(mEglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (mEglContext == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }

    mEglSurface = eglCreateWindowSurface(mEglDisplay, config, mInputSurface, nullptr);
    if (mEglSurface == EGL_NO_SURFACE) {
        LOGE("Failed to create EGL window surface");
        return false;
    }

    if (!eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext)) {
        LOGE("Failed to make EGL context current");
        return false;
    }

    return true;
}

ANativeWindow* DirectLogEncoder::getInputSurface() {
    return mInputSurface;
}

bool DirectLogEncoder::makeCurrent() {
    if (mEglDisplay == EGL_NO_DISPLAY || mEglSurface == EGL_NO_SURFACE || mEglContext == EGL_NO_CONTEXT) {
        return false;
    }
    return eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext) == EGL_TRUE;
}

void DirectLogEncoder::clearCurrent() {
    if (mEglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}

bool DirectLogEncoder::swapBuffers(int64_t presentationTimeNs) {
    if (mEglDisplay == EGL_NO_DISPLAY || mEglSurface == EGL_NO_SURFACE) {
        return false;
    }

    typedef EGLBoolean (*PFNEGLPRESENTATIONTIMEANDROIDPROC)(EGLDisplay, EGLSurface, khronos_stime_nanoseconds_t);
    static PFNEGLPRESENTATIONTIMEANDROIDPROC eglPresentationTimeANDROID =
        reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(eglGetProcAddress("eglPresentationTimeANDROID"));

    if (eglPresentationTimeANDROID && presentationTimeNs > 0) {
        eglPresentationTimeANDROID(mEglDisplay, mEglSurface, presentationTimeNs);
    }

    return eglSwapBuffers(mEglDisplay, mEglSurface) == EGL_TRUE;
}

bool DirectLogEncoder::setupAudioEncoder() {
    mAudioCodec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (!mAudioCodec) {
        LOGE("Failed to create audio encoder");
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, 128000);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, 48000);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_AAC_PROFILE, 2); // AAC LC

    media_status_t status = AMediaCodec_configure(mAudioCodec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        LOGE("Failed to configure audio encoder: %d", status);
        return false;
    }

    return true;
}

void DirectLogEncoder::feedAudioPCM(uint8_t* pcmBuffer, int size, int64_t presentationTimeUs) {
    if (!mIsRunning || !mAudioCodec) return;

    ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(mAudioCodec, 2000);
    if (bufIdx >= 0) {
        size_t bufSize;
        uint8_t* buf = AMediaCodec_getInputBuffer(mAudioCodec, bufIdx, &bufSize);
        if (buf && size <= bufSize) {
            memcpy(buf, pcmBuffer, size);
            AMediaCodec_queueInputBuffer(mAudioCodec, bufIdx, 0, size, presentationTimeUs, 0);
        } else {
            LOGE("Audio input buffer too small or null");
        }
    } else {
        LOGE("Failed to dequeue audio input buffer: %zd", bufIdx);
    }
}

bool DirectLogEncoder::start() {
    std::lock_guard<std::mutex> lock(mMutex);

    if (mIsRunning) return false;

    if (AMediaCodec_start(mVideoCodec) != AMEDIA_OK) {
        LOGE("Failed to start video codec");
        return false;
    }

    if (AMediaCodec_start(mAudioCodec) != AMEDIA_OK) {
        LOGE("Failed to start audio codec");
        return false;
    }

    mIsRunning = true;
    mHasVideoEos = false;
    mHasAudioEos = false;
    mMuxerStarted = false;
    mVideoTrackIndex = -1;
    mAudioTrackIndex = -1;

    mMuxerThread = std::thread(&DirectLogEncoder::muxerThreadFunc, this);

    return true;
}

void DirectLogEncoder::muxerThreadFunc() {
    AMediaCodecBufferInfo videoInfo;
    AMediaCodecBufferInfo audioInfo;

    while (mIsRunning || !mHasVideoEos || !mHasAudioEos) {
        // Process Video
        if (!mHasVideoEos) {
            ssize_t outVideoIdx = AMediaCodec_dequeueOutputBuffer(mVideoCodec, &videoInfo, 2000);
            if (outVideoIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (mMuxerStarted) {
                    LOGE("Video format changed after muxer started");
                } else {
                    AMediaFormat* format = AMediaCodec_getOutputFormat(mVideoCodec);
                    mVideoTrackIndex = AMediaMuxer_addTrack(mMuxer, format);
                    AMediaFormat_delete(format);
                    if (mVideoTrackIndex >= 0 && mAudioTrackIndex >= 0) {
                        AMediaMuxer_start(mMuxer);
                        mMuxerStarted = true;
                    }
                }
            } else if (outVideoIdx >= 0) {
                if ((videoInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                    mHasVideoEos = true;
                }
                if (videoInfo.size > 0 && mMuxerStarted) {
                    size_t bufSize;
                    uint8_t* buf = AMediaCodec_getOutputBuffer(mVideoCodec, outVideoIdx, &bufSize);
                    if (buf) {
                        AMediaMuxer_writeSampleData(mMuxer, mVideoTrackIndex, buf, &videoInfo);
                    }
                }
                AMediaCodec_releaseOutputBuffer(mVideoCodec, outVideoIdx, false);
            }
        }

        // Process Audio
        if (!mHasAudioEos) {
            ssize_t outAudioIdx = AMediaCodec_dequeueOutputBuffer(mAudioCodec, &audioInfo, 2000);
            if (outAudioIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (mMuxerStarted) {
                    LOGE("Audio format changed after muxer started");
                } else {
                    AMediaFormat* format = AMediaCodec_getOutputFormat(mAudioCodec);
                    mAudioTrackIndex = AMediaMuxer_addTrack(mMuxer, format);
                    AMediaFormat_delete(format);
                    if (mVideoTrackIndex >= 0 && mAudioTrackIndex >= 0) {
                        AMediaMuxer_start(mMuxer);
                        mMuxerStarted = true;
                    }
                }
            } else if (outAudioIdx >= 0) {
                if ((audioInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                    mHasAudioEos = true;
                }
                if (audioInfo.size > 0 && mMuxerStarted) {
                    size_t bufSize;
                    uint8_t* buf = AMediaCodec_getOutputBuffer(mAudioCodec, outAudioIdx, &bufSize);
                    if (buf) {
                        AMediaMuxer_writeSampleData(mMuxer, mAudioTrackIndex, buf, &audioInfo);
                    }
                }
                AMediaCodec_releaseOutputBuffer(mAudioCodec, outAudioIdx, false);
            }
        }
    }
}

void DirectLogEncoder::stop() {
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mIsRunning) return;

    mIsRunning = false;

    if (mVideoCodec) {
        DL_AMediaCodec_signalEndOfInputStream(mVideoCodec);
    }

    if (mAudioCodec) {
        ssize_t inIdx = AMediaCodec_dequeueInputBuffer(mAudioCodec, 2000);
        if (inIdx >= 0) {
            AMediaCodec_queueInputBuffer(mAudioCodec, inIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        }
    }

    if (mMuxerThread.joinable()) {
        mMuxerThread.join();
    }
}

void DirectLogEncoder::release() {
    stop();

    std::lock_guard<std::mutex> lock(mMutex);

    if (mVideoCodec) {
        AMediaCodec_stop(mVideoCodec);
        AMediaCodec_delete(mVideoCodec);
        mVideoCodec = nullptr;
    }

    if (mAudioCodec) {
        AMediaCodec_stop(mAudioCodec);
        AMediaCodec_delete(mAudioCodec);
        mAudioCodec = nullptr;
    }

    if (mMuxer) {
        if (mMuxerStarted) {
            AMediaMuxer_stop(mMuxer);
        }
        AMediaMuxer_delete(mMuxer);
        mMuxer = nullptr;
        mMuxerStarted = false;
    }

    if (mEglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mEglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(mEglDisplay, mEglContext);
            mEglContext = EGL_NO_CONTEXT;
        }
        if (mEglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(mEglDisplay, mEglSurface);
            mEglSurface = EGL_NO_SURFACE;
        }
        eglTerminate(mEglDisplay);
        mEglDisplay = EGL_NO_DISPLAY;
    }

    if (mInputSurface) {
        ANativeWindow_release(mInputSurface);
        mInputSurface = nullptr;
    }
}

} // namespace motioncam

extern "C" JNIEXPORT jlong JNICALL
Java_com_motioncam_camera_DirectLogEncoder_initNative(JNIEnv *env, jobject thiz) {
    return reinterpret_cast<jlong>(new motioncam::DirectLogEncoder());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_DirectLogEncoder_configureNative(
        JNIEnv *env, jobject thiz, jlong handle, jstring videoCodecMime, jint width, jint height,
        jint bitrate, jint fps, jstring outputFilePath) {

    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (!encoder) return false;

    const char* mimeCStr = env->GetStringUTFChars(videoCodecMime, nullptr);
    const char* pathCStr = env->GetStringUTFChars(outputFilePath, nullptr);

    bool result = encoder->configure(mimeCStr, width, height, bitrate, fps, pathCStr);

    env->ReleaseStringUTFChars(videoCodecMime, mimeCStr);
    env->ReleaseStringUTFChars(outputFilePath, pathCStr);

    return result;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_motioncam_camera_DirectLogEncoder_getInputSurfaceNative(JNIEnv *env, jobject thiz, jlong handle) {
    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (!encoder) return nullptr;

    ANativeWindow* window = encoder->getInputSurface();
    if (!window) return nullptr;

    // The NDK has no ANativeWindow -> android.view.Surface conversion; callers must
    // use the native window directly (EGL) rather than a Java Surface.
    LOGE("getInputSurfaceNative: Java Surface conversion is not supported by the NDK");
    return nullptr;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_DirectLogEncoder_startNative(JNIEnv *env, jobject thiz, jlong handle) {
    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (!encoder) return false;
    return encoder->start();
}

extern "C" JNIEXPORT void JNICALL
Java_com_motioncam_camera_DirectLogEncoder_feedAudioNative(
        JNIEnv *env, jobject thiz, jlong handle, jobject pcmBuffer, jint size, jlong presentationTimeUs) {

    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (!encoder || !pcmBuffer) return;

    void* bufferAddress = env->GetDirectBufferAddress(pcmBuffer);
    if (bufferAddress) {
        encoder->feedAudioPCM(static_cast<uint8_t*>(bufferAddress), size, presentationTimeUs);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_motioncam_camera_DirectLogEncoder_stopNative(JNIEnv *env, jobject thiz, jlong handle) {
    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (encoder) {
        encoder->stop();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_motioncam_camera_DirectLogEncoder_releaseNative(JNIEnv *env, jobject thiz, jlong handle) {
    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (encoder) {
        delete encoder;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_DirectLogEncoder_makeCurrentNative(JNIEnv *env, jobject thiz, jlong handle) {
    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (!encoder) return false;
    return encoder->makeCurrent();
}

extern "C" JNIEXPORT void JNICALL
Java_com_motioncam_camera_DirectLogEncoder_clearCurrentNative(JNIEnv *env, jobject thiz, jlong handle) {
    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (!encoder) return;
    encoder->clearCurrent();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_DirectLogEncoder_swapBuffersNative(JNIEnv *env, jobject thiz, jlong handle, jlong presentationTimeNs) {
    auto encoder = reinterpret_cast<motioncam::DirectLogEncoder*>(handle);
    if (!encoder) return false;
    return encoder->swapBuffers(presentationTimeNs);
}
