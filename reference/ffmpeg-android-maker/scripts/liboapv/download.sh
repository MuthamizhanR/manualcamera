#!/usr/bin/env bash

source ${SCRIPTS_DIR}/common-functions.sh

APV_VERSION=0.2.0.4

downloadTarArchive \
  "liboapv" \
  "https://github.com/AcademySoftwareFoundation/openapv/archive/refs/tags/v${APV_VERSION}.tar.gz"

LIBRARY_NAME=liboapv
export SOURCES_DIR_${LIBRARY_NAME}=$(pwd)/openapv-${APV_VERSION}
