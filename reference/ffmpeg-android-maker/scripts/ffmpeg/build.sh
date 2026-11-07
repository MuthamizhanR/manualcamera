#!/usr/bin/env bash

case $ANDROID_ABI in
  x86)
    # Disabling assembler optimizations, because they have text relocations
    EXTRA_BUILD_CONFIGURATION_FLAGS="$EXTRA_BUILD_CONFIGURATION_FLAGS --disable-asm"
    ;;
  x86_64)
    EXTRA_BUILD_CONFIGURATION_FLAGS="$EXTRA_BUILD_CONFIGURATION_FLAGS --x86asmexe=${FAM_YASM}"
    ;;
esac

if [ "$FFMPEG_GPL_ENABLED" = true ] ; then
    EXTRA_BUILD_CONFIGURATION_FLAGS="$EXTRA_BUILD_CONFIGURATION_FLAGS --enable-gpl"
fi

# Preparing flags for enabling requested libraries
ADDITIONAL_COMPONENTS=
VULKAN_ENABLED=false
for LIBARY_NAME in ${FFMPEG_EXTERNAL_LIBRARIES[@]}
do
  # glslang enables Vulkan support, not a direct FFmpeg library
  if [ "$LIBARY_NAME" = "glslang" ]; then
    VULKAN_ENABLED=true
  else
    ADDITIONAL_COMPONENTS+=" --enable-$LIBARY_NAME"
  fi
done

# Referencing dependencies without pkgconfig
DEP_CFLAGS="-I${BUILD_DIR_EXTERNAL}/${ANDROID_ABI}/include"
DEP_LD_FLAGS="-L${BUILD_DIR_EXTERNAL}/${ANDROID_ABI}/lib $FFMPEG_EXTRA_LD_FLAGS"

# Vulkan/glslang configuration
if [ "$VULKAN_ENABLED" = true ]; then
  VULKAN_FLAGS="--enable-vulkan"

  # Create vulkan.pc for FFmpeg to find Vulkan from Android NDK
  # Vulkan requires minimum API level 24 (Android 7.0)
  VULKAN_API_LEVEL=$ANDROID_PLATFORM
  if [ $VULKAN_API_LEVEL -lt 24 ]; then
    VULKAN_API_LEVEL=24
  fi

  # The library path follows NDK structure: lib/<triple>/<api>/
  case $ANDROID_ABI in
    armeabi-v7a) VULKAN_TRIPLE="arm-linux-androideabi" ;;
    arm64-v8a)   VULKAN_TRIPLE="aarch64-linux-android" ;;
    x86)         VULKAN_TRIPLE="i686-linux-android" ;;
    x86_64)      VULKAN_TRIPLE="x86_64-linux-android" ;;
  esac

  cat > ${BUILD_DIR_EXTERNAL}/${ANDROID_ABI}/lib/pkgconfig/vulkan.pc << VULKAN_PC
prefix=${BUILD_DIR_EXTERNAL}/${ANDROID_ABI}
exec_prefix=\${prefix}
libdir=${SYSROOT_PATH}/usr/lib/${VULKAN_TRIPLE}/${VULKAN_API_LEVEL}
includedir=\${prefix}/include

Name: Vulkan-Loader
Description: Vulkan Loader with headers from Khronos
Version: 1.4.304
Libs: -L\${libdir} -lvulkan
Cflags: -I\${includedir}
VULKAN_PC
else
  VULKAN_FLAGS="--disable-vulkan"
fi

# Android 15 with 16 kb page size support
# https://developer.android.com/guide/practices/page-sizes#compile-r27
EXTRA_LDFLAGS="-Wl,-z,max-page-size=16384 $DEP_LD_FLAGS"

./configure \
  --prefix=${BUILD_DIR_FFMPEG}/${ANDROID_ABI} \
  --enable-cross-compile \
  --target-os=android \
  --arch=${TARGET_TRIPLE_MACHINE_ARCH} \
  --sysroot=${SYSROOT_PATH} \
  --cc=${FAM_CC} \
  --cxx=${FAM_CXX} \
  --ld=${FAM_LD} \
  --ar=${FAM_AR} \
  --as=${FAM_CC} \
  --nm=${FAM_NM} \
  --ranlib=${FAM_RANLIB} \
  --strip=${FAM_STRIP} \
  --extra-cflags="-O3 -fPIC $DEP_CFLAGS" \
  --extra-ldflags="$EXTRA_LDFLAGS" \
  --enable-static \
  --disable-shared \
  ${VULKAN_FLAGS} \
  --pkg-config=${PKG_CONFIG_EXECUTABLE} \
  ${EXTRA_BUILD_CONFIGURATION_FLAGS} \
  $ADDITIONAL_COMPONENTS || exit 1

${MAKE_EXECUTABLE} clean
${MAKE_EXECUTABLE} -j${HOST_NPROC}
${MAKE_EXECUTABLE} install
