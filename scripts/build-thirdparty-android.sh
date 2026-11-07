#!/usr/bin/env bash
#
# Cross-compiles the prebuilt native dependencies required by
# MotionCam-Android/MotionCam/CMakeLists.txt (OpenCV, expat, exiv2, zstd) with
# the Android NDK and installs them to libMotionCam/thirdparty/.
#
# Linux/macOS compatible and CI friendly (no Homebrew, no Halide).
#
# Usage:
#   ANDROID_NDK=/path/to/ndk/22.1.7171670 scripts/build-thirdparty-android.sh
#
# Optional environment:
#   ANDROID_ABI   Target ABI (default: arm64-v8a)
#   WORK_DIR      Scratch directory for sources/builds (default: <repo>/.thirdparty-build)
#   JOBS          Parallel jobs (default: number of CPUs)
#
# Each dependency drops a marker file in the install dir, so re-running (or
# restoring the thirdparty dirs from a CI cache) skips finished work.
#
set -euo pipefail

OPENCV_VERSION="4.5.2"
LIBEXPAT_VERSION="2.2.10"
LIBEXIV2_VERSION="0.27.3"
ZSTD_VERSION="1.4.9"

ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
ANDROID_API="${ANDROID_API:-21}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRDPARTY="${REPO_ROOT}/libMotionCam/thirdparty"
WORK_DIR="${WORK_DIR:-${REPO_ROOT}/.thirdparty-build}"
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2) )}"

if [[ -z "${ANDROID_NDK:-}" || ! -f "${ANDROID_NDK}/build/cmake/android.toolchain.cmake" ]]; then
    echo "ERROR: ANDROID_NDK must point to an Android NDK installation." >&2
    exit 1
fi

TOOLCHAIN_FILE="${ANDROID_NDK}/build/cmake/android.toolchain.cmake"

# Flags shared by every dependency.
ANDROID_CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"
    -DANDROID_ABI="${ANDROID_ABI}"
    -DANDROID_PLATFORM="android-${ANDROID_API}"
    -DANDROID_STL=c++_shared
)

mkdir -p "${WORK_DIR}" "${THIRDPARTY}"

fetch() { # url dest
    if [[ ! -s "$2" ]]; then
        curl -fL --retry 3 --retry-delay 5 "$1" --output "$2"
    fi
}

build_zstd() {
    local marker="${THIRDPARTY}/zstd/.built-${ZSTD_VERSION}-${ANDROID_ABI}"
    [[ -f "${marker}" ]] && { echo "== zstd up to date"; return; }
    echo "== Building zstd ${ZSTD_VERSION}"

    pushd "${WORK_DIR}" >/dev/null
    fetch "https://github.com/facebook/zstd/archive/refs/tags/v${ZSTD_VERSION}.tar.gz" "zstd-${ZSTD_VERSION}.tar.gz"
    rm -rf "zstd-${ZSTD_VERSION}"
    tar -xf "zstd-${ZSTD_VERSION}.tar.gz"

    cmake -S "zstd-${ZSTD_VERSION}/build/cmake" -B "zstd-${ZSTD_VERSION}/build-android" \
        "${ANDROID_CMAKE_ARGS[@]}" \
        -DCMAKE_INSTALL_PREFIX="${WORK_DIR}/install/zstd" \
        -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_SHARED=OFF \
        -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_CONTRIB=OFF -DZSTD_BUILD_TESTS=OFF
    cmake --build "zstd-${ZSTD_VERSION}/build-android" -j "${JOBS}"
    cmake --install "zstd-${ZSTD_VERSION}/build-android"

    rm -rf "${THIRDPARTY}/zstd"
    mkdir -p "${THIRDPARTY}/zstd/lib" "${THIRDPARTY}/zstd/include"
    cp -a "${WORK_DIR}/install/zstd/include/." "${THIRDPARTY}/zstd/include/"
    cp -a "${WORK_DIR}/install/zstd/lib/libzstd.a" "${THIRDPARTY}/zstd/lib/"
    touch "${marker}"
    popd >/dev/null
}

build_expat() {
    local marker="${THIRDPARTY}/expat/.built-${LIBEXPAT_VERSION}-${ANDROID_ABI}"
    [[ -f "${marker}" ]] && { echo "== expat up to date"; return; }
    echo "== Building expat ${LIBEXPAT_VERSION}"

    pushd "${WORK_DIR}" >/dev/null
    fetch "https://github.com/libexpat/libexpat/releases/download/R_${LIBEXPAT_VERSION//./_}/expat-${LIBEXPAT_VERSION}.tar.gz" \
        "expat-${LIBEXPAT_VERSION}.tar.gz"
    rm -rf "expat-${LIBEXPAT_VERSION}"
    tar -xf "expat-${LIBEXPAT_VERSION}.tar.gz"

    cmake -S "expat-${LIBEXPAT_VERSION}" -B "expat-${LIBEXPAT_VERSION}/build-android" \
        "${ANDROID_CMAKE_ARGS[@]}" \
        -DCMAKE_INSTALL_PREFIX="${WORK_DIR}/install/expat" \
        -DEXPAT_BUILD_DOCS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF \
        -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_SHARED_LIBS=OFF
    cmake --build "expat-${LIBEXPAT_VERSION}/build-android" -j "${JOBS}"
    cmake --install "expat-${LIBEXPAT_VERSION}/build-android"

    rm -rf "${THIRDPARTY}/expat"
    mkdir -p "${THIRDPARTY}/expat/lib" "${THIRDPARTY}/expat/include"
    cp -a "${WORK_DIR}/install/expat/include/." "${THIRDPARTY}/expat/include/"
    cp -a "${WORK_DIR}"/install/expat/lib/libexpat*.a "${THIRDPARTY}/expat/lib/libexpat.a"
    touch "${marker}"
    popd >/dev/null
}

build_exiv2() {
    local marker="${THIRDPARTY}/exiv2/.built-${LIBEXIV2_VERSION}-${ANDROID_ABI}"
    [[ -f "${marker}" ]] && { echo "== exiv2 up to date"; return; }
    echo "== Building exiv2 ${LIBEXIV2_VERSION}"

    pushd "${WORK_DIR}" >/dev/null
    fetch "https://github.com/Exiv2/exiv2/archive/refs/tags/v${LIBEXIV2_VERSION}.tar.gz" "exiv2-${LIBEXIV2_VERSION}.tar.gz"
    rm -rf "exiv2-${LIBEXIV2_VERSION}"
    tar -xf "exiv2-${LIBEXIV2_VERSION}.tar.gz"

    cmake -S "exiv2-${LIBEXIV2_VERSION}" -B "exiv2-${LIBEXIV2_VERSION}/build-android" \
        "${ANDROID_CMAKE_ARGS[@]}" \
        -DCMAKE_INSTALL_PREFIX="${WORK_DIR}/install/exiv2" \
        -DBUILD_SHARED_LIBS=OFF \
        -DEXIV2_BUILD_SAMPLES=OFF -DEXIV2_BUILD_EXIV2_COMMAND=OFF -DEXIV2_BUILD_UNIT_TESTS=OFF \
        -DEXIV2_ENABLE_NLS=OFF -DEXIV2_ENABLE_VIDEO=OFF -DEXIV2_ENABLE_WEBREADY=OFF \
        -DEXPAT_LIBRARY="${THIRDPARTY}/expat/lib/libexpat.a" \
        -DEXPAT_INCLUDE_DIR="${THIRDPARTY}/expat/include"
    cmake --build "exiv2-${LIBEXIV2_VERSION}/build-android" -j "${JOBS}"
    cmake --install "exiv2-${LIBEXIV2_VERSION}/build-android"

    rm -rf "${THIRDPARTY}/exiv2"
    mkdir -p "${THIRDPARTY}/exiv2/lib" "${THIRDPARTY}/exiv2/include"
    cp -a "${WORK_DIR}/install/exiv2/include/." "${THIRDPARTY}/exiv2/include/"
    cp -a "${WORK_DIR}"/install/exiv2/lib/*.a "${THIRDPARTY}/exiv2/lib/"
    touch "${marker}"
    popd >/dev/null
}

build_opencv() {
    local marker="${THIRDPARTY}/opencv/.built-${OPENCV_VERSION}-${ANDROID_ABI}"
    [[ -f "${marker}" ]] && { echo "== OpenCV up to date"; return; }
    echo "== Building OpenCV ${OPENCV_VERSION}"

    pushd "${WORK_DIR}" >/dev/null
    fetch "https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.tar.gz" "opencv-${OPENCV_VERSION}.tar.gz"
    fetch "https://github.com/opencv/opencv_contrib/archive/${OPENCV_VERSION}.tar.gz" "opencv_contrib-${OPENCV_VERSION}.tar.gz"
    rm -rf "opencv-${OPENCV_VERSION}" "opencv_contrib-${OPENCV_VERSION}"
    tar -xf "opencv-${OPENCV_VERSION}.tar.gz"
    tar -xf "opencv_contrib-${OPENCV_VERSION}.tar.gz"

    # Only the modules libMotionCam links against (plus their dependencies).
    local modules="core,imgproc,imgcodecs,calib3d,features2d,flann,video,photo,ximgproc,xfeatures2d"

    cmake -S "opencv-${OPENCV_VERSION}" -B "opencv-${OPENCV_VERSION}/build-android" \
        "${ANDROID_CMAKE_ARGS[@]}" \
        -DCMAKE_INSTALL_PREFIX="${WORK_DIR}/install/opencv" \
        -DOPENCV_EXTRA_MODULES_PATH="${WORK_DIR}/opencv_contrib-${OPENCV_VERSION}/modules" \
        -DBUILD_LIST="${modules}" \
        -DWITH_TBB=ON -DCPU_BASELINE=NEON -DENABLE_NEON=ON \
        -DBUILD_JAVA=OFF -DBUILD_ANDROID_PROJECTS=OFF -DBUILD_ANDROID_EXAMPLES=OFF \
        -DBUILD_DOCS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_TESTS=OFF -DBUILD_opencv_apps=OFF
    cmake --build "opencv-${OPENCV_VERSION}/build-android" -j "${JOBS}"
    cmake --install "opencv-${OPENCV_VERSION}/build-android"

    local install="${WORK_DIR}/install/opencv"
    local dest="${THIRDPARTY}/opencv"
    rm -rf "${dest}"
    mkdir -p "${dest}/libs" "${dest}/include" "${dest}/thirdparty"
    cp -a "${WORK_DIR}/opencv-${OPENCV_VERSION}/build-android/lib/." "${dest}/libs/."
    cp -a "${install}/sdk/native/jni/include/." "${dest}/include/."
    cp -a "${install}/sdk/native/3rdparty/." "${dest}/thirdparty/."
    touch "${marker}"
    popd >/dev/null
}

# expat must precede exiv2.
build_zstd
build_expat
build_exiv2
build_opencv

echo "== Third-party dependencies installed in ${THIRDPARTY}"
