package com.motioncam.processor;

import android.util.Log;

public class NativeProcessor {
    static {
        try {
            System.loadLibrary("native-camera-host");
        }
        catch (Throwable ignored) {
        }
    }

    public boolean processInMemory(String outputPath, NativeProcessorProgressListener listener) {
        try {
            return ProcessInMemory(outputPath, listener);
        }
        catch (UnsatisfiedLinkError e) {
            Log.e("MotionCam", "ProcessInMemory not available", e);
            return false;
        }
    }

    public void processFile(String inputPath, String outputPath, NativeProcessorProgressListener listener) {
        try {
            ProcessFile(inputPath, outputPath, listener);
        }
        catch (UnsatisfiedLinkError e) {
            Log.e("MotionCam", "ProcessFile not available", e);
        }
    }

    native boolean ProcessInMemory(String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessFile(String inputPath, String outputPath, NativeProcessorProgressListener progressListener);
    native String GetLastError();
}
