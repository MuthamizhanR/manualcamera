package com.motioncam.camera;

import android.view.Surface;
import java.nio.ByteBuffer;

public class DirectLogEncoder {
    private long mNativeHandle = 0;
    private Surface mInputSurface = null;

    public DirectLogEncoder() {
        mNativeHandle = initNative();
    }

    public boolean configure(String videoCodecMime, int width, int height, int bitrate, int fps, String outputFilePath) {
        if (mNativeHandle != 0) {
            return configureNative(mNativeHandle, videoCodecMime, width, height, bitrate, fps, outputFilePath);
        }
        return false;
    }

    public Surface getInputSurface() {
        if (mNativeHandle != 0 && mInputSurface == null) {
            mInputSurface = getInputSurfaceNative(mNativeHandle);
        }
        return mInputSurface;
    }

    public boolean start() {
        if (mNativeHandle != 0) {
            return startNative(mNativeHandle);
        }
        return false;
    }

    public void feedAudioPCM(ByteBuffer pcmBuffer, int size, long presentationTimeUs) {
        if (mNativeHandle != 0) {
            feedAudioNative(mNativeHandle, pcmBuffer, size, presentationTimeUs);
        }
    }

    public boolean makeCurrent() {
        if (mNativeHandle != 0) {
            return makeCurrentNative(mNativeHandle);
        }
        return false;
    }

    public void clearCurrent() {
        if (mNativeHandle != 0) {
            clearCurrentNative(mNativeHandle);
        }
    }

    public boolean swapBuffers(long presentationTimeNs) {
        if (mNativeHandle != 0) {
            return swapBuffersNative(mNativeHandle, presentationTimeNs);
        }
        return false;
    }

    public void stop() {
        if (mNativeHandle != 0) {
            stopNative(mNativeHandle);
        }
    }

    public void release() {
        if (mNativeHandle != 0) {
            releaseNative(mNativeHandle);
            mNativeHandle = 0;
        }
        if (mInputSurface != null) {
            mInputSurface.release();
            mInputSurface = null;
        }
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            release();
        } finally {
            super.finalize();
        }
    }

    private native long initNative();
    private native boolean configureNative(long handle, String videoCodecMime, int width, int height, int bitrate, int fps, String outputFilePath);
    private native Surface getInputSurfaceNative(long handle);
    private native boolean makeCurrentNative(long handle);
    private native void clearCurrentNative(long handle);
    private native boolean swapBuffersNative(long handle, long presentationTimeNs);
    private native boolean startNative(long handle);
    private native void feedAudioNative(long handle, ByteBuffer pcmBuffer, int size, long presentationTimeUs);
    private native void stopNative(long handle);
    private native void releaseNative(long handle);
}
