#!/bin/bash
# * Project: OpenHU
# * This file is part of OpenHU, based on the openauto project.
# * Copyright (C) 2025 OpenCarDev Team
# * Copyright (C) 2026 Leon Strauß
# *
# *  OpenHU is free software: you can redistribute it and/or modify
# *  it under the terms of the GNU General Public License as published by
# *  the Free Software Foundation; either version 3 of the License, or
# *  (at your option) any later version.
# *
# *  OpenHU is distributed in the hope that it will be useful,
# *  but WITHOUT ANY WARRANTY; without even the implied warranty of
# *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# *  GNU General Public License for more details.
# *
# *  You should have received a copy of the GNU General Public License
# *  along with OpenHU. If not, see <https://www.gnu.org/licenses/>.

set -e

# Script to build OpenHU consistently across Docker and local environments
# Usage: ./build.sh [release|debug] [--clean] [--package] [--output-dir DIR]

# Default values
BUILD_TYPE="release"
CLEAN_BUILD=false
PACKAGE=false
OUTPUT_DIR="/output"
WITH_AASDK=false
AASDK_BRANCH=""
AASDK_PATH=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SCRIPT_DIR}"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        release|Release|RELEASE)
            BUILD_TYPE="release"
            shift
            ;;
        debug|Debug|DEBUG)
            BUILD_TYPE="debug"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --package)
            PACKAGE=true
            shift
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --with-aasdk)
            WITH_AASDK=true
            shift
            ;;
        --aasdk-branch)
            AASDK_BRANCH="$2"
            WITH_AASDK=true
            shift 2
            ;;
        --aasdk-path)
            AASDK_PATH="$2"
            WITH_AASDK=true
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [release|debug] [OPTIONS]"
            echo ""
            echo "Build types:"
            echo "  release        Build release version (default)"
            echo "  debug          Build debug version with symbols"
            echo ""
            echo "Options:"
            echo "  --clean        Clean build directory before building"
            echo "  --package      Create DEB packages after building"
            echo "  --output-dir   Directory to copy packages (default: /output)"
            echo "  --with-aasdk   Install the libaasdk packages first (newest release that fits OPENHU_AASDK_VERSION)"
            echo "  --aasdk-branch Package and install AASDK from this branch instead of a release"
            echo "  --aasdk-path   Package and install AASDK from a local source tree instead of a release"
            echo "  --help         Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 release --package"
            echo "  $0 debug --clean"
            echo "  $0 release --package --output-dir ./packages"
            echo "  $0 release --with-aasdk --package --output-dir ./packages"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Install the libaasdk packages if requested: from a release, or packaged from a branch / source tree
AASDK_DEBS="${SOURCE_DIR}/aasdk-debs"
if [ "$WITH_AASDK" = true ]; then
    echo ""
    rm -rf "${AASDK_DEBS}"
    mkdir -p "${AASDK_DEBS}"
    if [ -z "$AASDK_PATH" ] && [ -z "$AASDK_BRANCH" ]; then
        echo "Downloading the AASDK release..."
        echo "AASDK $(bash "${SOURCE_DIR}/scripts/fetch-aasdk.sh" "${AASDK_DEBS}")"
    else
        if [ -z "$AASDK_PATH" ]; then
            echo "Cloning AASDK ${AASDK_BRANCH} branch..."
            git clone --depth 1 --branch "${AASDK_BRANCH}" https://github.com/average-web-dev/aasdk.git "${AASDK_DEBS}/src"
            AASDK_PATH="${AASDK_DEBS}/src"
        fi
        echo "Packaging AASDK from ${AASDK_PATH}..."
        cd "${AASDK_PATH}"
        # Always release: a debug build is packaged as libaasdk-dbg, which does not satisfy the Depends of OpenHU
        BUILD_DIR="${AASDK_DEBS}/build" BUILD_TESTS=OFF \
            CMAKE_EXTRA_ARGS="-DCPACK_DEBIAN_PACKAGE_RELEASE=$(bash scripts/distro_release.sh)" \
            bash ./build.sh release package --skip-protobuf --skip-absl
        cp "${AASDK_DEBS}"/build/packages/libaasdk*.deb "${AASDK_DEBS}/"
        cd "${SOURCE_DIR}"
    fi
    echo "Installing the AASDK packages..."
    APT_GET="apt-get"
    [ "$(id -u)" -ne 0 ] && APT_GET="sudo apt-get"
    ${APT_GET} install -y "${AASDK_DEBS}"/libaasdk*.deb
    echo "AASDK packages installed."
fi

# Determine build directory and CMake build type
if [ "$BUILD_TYPE" = "debug" ]; then
    BUILD_DIR="${SOURCE_DIR}/build-debug"
    CMAKE_BUILD_TYPE="Debug"
    CMAKE_CXX_FLAGS="-g3 -O0"
    echo "=== Building OpenAuto (Debug) ==="
else
    BUILD_DIR="${SOURCE_DIR}/build-release"
    CMAKE_BUILD_TYPE="Release"
    CMAKE_CXX_FLAGS=""
    echo "=== Building OpenAuto (Release) ==="
fi

echo "Source directory: ${SOURCE_DIR}"
echo "Build directory: ${BUILD_DIR}"
echo "Build type: ${CMAKE_BUILD_TYPE}"
echo "Package: ${PACKAGE}"

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo ""
    echo "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

# Create build directory, a build directory from another generator (Makefiles) is discarded
if [ -f "${BUILD_DIR}/CMakeCache.txt" ] && [ ! -f "${BUILD_DIR}/build.ninja" ]; then
    rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"

# Compute distro-specific release suffix
if [ -f "${SOURCE_DIR}/scripts/distro_release.sh" ]; then
    DISTRO_DEB_RELEASE=$(bash "${SOURCE_DIR}/scripts/distro_release.sh")
    echo "Distro release suffix: ${DISTRO_DEB_RELEASE}"
else
    DISTRO_DEB_RELEASE=""
    echo "Warning: distro_release.sh not found, using default release suffix"
fi

# Configure CMake
echo ""
echo "Configuring with CMake..."
CMAKE_ARGS=(
    -S "${SOURCE_DIR}"
    -B "${BUILD_DIR}"
    -G Ninja
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)

if [ -n "$CMAKE_CXX_FLAGS" ]; then
    CMAKE_ARGS+=(-DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS}")
fi

if [ -n "$DISTRO_DEB_RELEASE" ]; then
    CMAKE_ARGS+=(-DCPACK_DEBIAN_PACKAGE_RELEASE="${DISTRO_DEB_RELEASE}")
fi

CMAKE_ARGS+=(-DCPACK_PROJECT_CONFIG_FILE="${SOURCE_DIR}/cmake_modules/CPackProjectConfig.cmake")

# Run CMake configuration
env DISTRO_DEB_RELEASE="${DISTRO_DEB_RELEASE}" cmake "${CMAKE_ARGS[@]}"

# Build
echo ""
echo "Building..."
cmake --build "${BUILD_DIR}"

echo ""
echo "✓ Build completed successfully"

# Package if requested
if [ "$PACKAGE" = true ]; then
    echo ""
    echo "Creating packages..."
    cd "${BUILD_DIR}"
    cpack -G DEB
    cd "${SOURCE_DIR}"
    
    # Copy packages to output directory
    if [ -n "$OUTPUT_DIR" ] && [ "$OUTPUT_DIR" != "${BUILD_DIR}" ]; then
        echo ""
        echo "Copying packages to ${OUTPUT_DIR}..."
        mkdir -p "${OUTPUT_DIR}"
        find "${BUILD_DIR}" -name "*.deb" -exec cp -v {} "${OUTPUT_DIR}/" \;
        if [ "$WITH_AASDK" = true ]; then
            cp -v "${AASDK_DEBS}"/libaasdk*.deb "${OUTPUT_DIR}/"
        fi
        echo ""
        echo "Packages in ${OUTPUT_DIR}:"
        ls -lh "${OUTPUT_DIR}"/*.deb 2>/dev/null || echo "No packages found"
    else
        echo ""
        echo "Packages in ${BUILD_DIR}:"
        find "${BUILD_DIR}" -name "*.deb" -ls
    fi
fi

echo ""
echo "=== Build Summary ==="
echo "Build type: ${CMAKE_BUILD_TYPE}"
echo "Build directory: ${BUILD_DIR}"
if [ -f "${BUILD_DIR}/bin/openhu" ]; then
    echo "Binary: ${BUILD_DIR}/bin/openhu"
fi
if [ -f "${BUILD_DIR}/btservice" ]; then
    echo "Binary: ${BUILD_DIR}/btservice"
fi

echo ""
echo "Done!"
