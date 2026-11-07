#!/usr/bin/env bash

source ${SCRIPTS_DIR}/common-functions.sh

GLSLANG_VERSION=16.1.0

downloadTarArchive \
  "glslang" \
  "https://github.com/KhronosGroup/glslang/archive/refs/tags/${GLSLANG_VERSION}.tar.gz"

LIBRARY_NAME=glslang
export SOURCES_DIR_${LIBRARY_NAME}=$(pwd)/glslang-${GLSLANG_VERSION}

# Use glslang's own script to fetch known-good versions of dependencies
# This ensures SPIRV-Tools and SPIRV-Headers are compatible
cd ${SOURCES_DIR_glslang}
python3 update_glslang_sources.py

# Download newer Vulkan headers (NDK has 1.3.275, FFmpeg 8 requires >= 1.3.277)
VULKAN_HEADERS_VERSION=vulkan-sdk-1.4.304.0
if [[ ! -d "${SOURCES_DIR_glslang}/external/vulkan-headers" ]]; then
  echo "Downloading Vulkan headers ${VULKAN_HEADERS_VERSION}..."
  git clone --depth 1 --branch ${VULKAN_HEADERS_VERSION} \
    https://github.com/KhronosGroup/Vulkan-Headers.git \
    ${SOURCES_DIR_glslang}/external/vulkan-headers
fi
export VULKAN_HEADERS_DIR=${SOURCES_DIR_glslang}/external/vulkan-headers/include
