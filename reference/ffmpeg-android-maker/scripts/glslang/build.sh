#!/usr/bin/env bash

# glslang doesn't support building while being in its root directory
CMAKE_BUILD_DIR=glslang_build_${ANDROID_ABI}
rm -rf ${CMAKE_BUILD_DIR}
mkdir ${CMAKE_BUILD_DIR}
cd ${CMAKE_BUILD_DIR}

${CMAKE_EXECUTABLE} .. \
  -DANDROID_PLATFORM=${ANDROID_PLATFORM} \
  -DANDROID_ABI=${ANDROID_ABI} \
  -DCMAKE_TOOLCHAIN_FILE=${SCRIPTS_DIR}/glslang/android.cmake \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
  -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_GLSLANG_BINARIES=OFF \
  -DENABLE_SPVREMAPPER=ON \
  -DGLSLANG_ENABLE_INSTALL=ON \
  -DSPIRV_SKIP_EXECUTABLES=ON \
  -DSPIRV_TOOLS_BUILD_STATIC=ON \
  -DENABLE_CTEST=OFF \
  -DENABLE_HLSL=OFF \
  -DBUILD_TESTING=OFF

${MAKE_EXECUTABLE} -j${HOST_NPROC}
${MAKE_EXECUTABLE} install

# Install newer Vulkan headers (required for FFmpeg 8+)
if [[ -d "../external/vulkan-headers/include/vulkan" ]]; then
  echo "Installing Vulkan headers..."
  cp -r ../external/vulkan-headers/include/vulkan ${INSTALL_DIR}/include/
  cp -r ../external/vulkan-headers/include/vk_video ${INSTALL_DIR}/include/ 2>/dev/null || true
fi

# Create empty libpthread.a stub for Android
# Android's bionic libc includes pthread, but FFmpeg's configure expects -lpthread
# Creating an empty archive satisfies the linker
echo "Creating libpthread stub for Android..."
${FAM_AR} rcs ${INSTALL_DIR}/lib/libpthread.a
