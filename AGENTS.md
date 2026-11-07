# MotionCam Next-Gen Architecture Guide for Jules

## Repository Layout
- `MotionCam-Android/`: The Android app (Java/Kotlin UI + JNI bridge + NDK CMake).
- `libMotionCam/`: The core C++ camera, ring-buffer, shader, and encoding engine.
- `reference/motioncam-decoder/`: Official modern MCRAW v3 C++ decoder (with CTest regression tests). Ground-truth spec for container v3, bit-packing, audio chunks, and Gyro/Accelerometer metadata (Item IDs 12 & 13).
- `reference/motioncam-player/`: Modern C++ debayering, highlight recovery, and color matrices.
- `reference/kompute/`: Vulkan mobile GPU compute framework.
- `reference/ffmpeg-android-maker/`: Scripts/configs for Android NDK FFmpeg (ProRes).
- `reference/motioncam-fs/`: Virtual CinemaDNG FUSE/FSKit filesystem consumer.
- `reference/jetpack-camera-app/`: Reference for Jetpack Compose camera UI/UX patterns ONLY (never use CameraX for the camera backend).

## Build & Verification Rules
1. **No Legacy Halide**: We have deprecated the 2021 Halide photo-burst pipeline (`libMotionCam/generators`) to focus 100% on manual RAW video (`.mcraw` v3) and real-time GPU DirectLog (10-bit HEVC/AV1/ProRes). Do not re-introduce Halide dependencies.
2. **Host C++ Tests (`ctest`)**: Pure C++ modules (like the MCRAW v3 encoder/decoder and ring buffers) must be testable on Linux x86_64 via CMake and `ctest --output-on-failure`.
3. **Android Build**: Always verify Android NDK compilation via `./gradlew assembleDebug`.
