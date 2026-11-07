#ifndef DIRECTLOGENCODER_H
#define DIRECTLOGENCODER_H

#include <jni.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace motioncam {

class DirectLogEncoder {
public:
    DirectLogEncoder();
    ~DirectLogEncoder();

    bool configure(const std::string& videoCodecMime, int width, int height, int bitrate, int fps, const std::string& outputFilePath);
    ANativeWindow* getInputSurface();
    bool makeCurrent();
    void clearCurrent();
    bool swapBuffers(int64_t presentationTimeNs);
    bool start();
    void feedAudioPCM(uint8_t* pcmBuffer, int size, int64_t presentationTimeUs);
    void stop();
    void release();

private:
    void muxerThreadFunc();
    bool setupVideoEncoder();
    bool setupAudioEncoder();
    bool setupEGL();

    std::string mVideoCodecMime;
    int mWidth;
    int mHeight;
    int mBitrate;
    int mFps;
    std::string mOutputFilePath;

    AMediaCodec* mVideoCodec;
    AMediaCodec* mAudioCodec;
    AMediaMuxer* mMuxer;

    ANativeWindow* mInputSurface;

    EGLDisplay mEglDisplay;
    EGLContext mEglContext;
    EGLSurface mEglSurface;

    int mVideoTrackIndex;
    int mAudioTrackIndex;

    std::atomic<bool> mIsRunning;
    std::atomic<bool> mMuxerStarted;

    std::thread mMuxerThread;
    std::mutex mMutex;

    bool mHasVideoEos;
    bool mHasAudioEos;
};

} // namespace motioncam

#endif // DIRECTLOGENCODER_H
