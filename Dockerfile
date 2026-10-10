# syntax=docker/dockerfile:1
# * Project: OpenAuto
# * This file is part of openauto project.
# * Copyright (C) 2025 OpenCarDev Team
# *
# *  openauto is free software: you can redistribute it and/or modify
# *  it under the terms of the GNU General Public License as published by
# *  the Free Software Foundation; either version 3 of the License, or
# *  (at your option) any later version.
# *
# *  openauto is distributed in the hope that it will be useful,
# *  but WITHOUT ANY WARRANTY; without even the implied warranty of
# *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# *  GNU General Public License for more details.
# *
# *  You should have received a copy of the GNU General Public License
# *  along with openauto. If not, see <http://www.gnu.org/licenses/>.

# Multi-stage build for OpenAuto with native compilation for each architecture
# This Dockerfile builds OpenAuto natively on each target platform using QEMU emulation

# Allow selecting Debian base (bookworm or trixie). Default to trixie.
ARG DEBIAN_VERSION=trixie

# Optional local AASDK source tree. Empty by default (-> AASDK is cloned from GitHub);
# override with: docker build --build-context aasdk=../aasdk-ref ...
FROM scratch AS aasdk

FROM debian:${DEBIAN_VERSION}-slim

# Build arguments
ARG TARGET_ARCH=$(dpkg --print-architecture)
ARG BUILD_TYPE=release
ARG DEBIAN_FRONTEND=noninteractive

# Set locale to avoid encoding issues
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# Install build dependencies and tools
RUN apt-get update && apt-get install -y \
    # Core build tools
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    lsb-release \
    curl \
    gnupg \
    ca-certificates \
    # Development libraries
    libboost-system-dev \
    libboost-log-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libusb-1.0-0-dev \
    libssl-dev \
    libblkid-dev \
    libgps-dev \
    libtag1-dev \
    librtaudio-dev \
    # Qt5 dependencies
    qtbase5-dev \
    qtmultimedia5-dev \
    qttools5-dev \
    qttools5-dev-tools \
    qtconnectivity5-dev \
    qtdeclarative5-dev \
    qtquickcontrols2-5-dev \
    libqt5websockets5-dev \
    qml-module-qtquick2 \
    qml-module-qtquick-window2 \
    qml-module-qtquick-layouts \
    qml-module-qtquick-controls2 \
    qml-module-qtmultimedia \
    libpulse-dev \
    # Packaging tools
    file \
    dpkg-dev \
    fakeroot \
    && rm -rf /var/lib/apt/lists/*

# Add OpenCarDev APT repository for libaasdk
ARG DEBIAN_VERSION

# Set working directory
WORKDIR /src

# Copy source code
COPY . .

# Debug: List what was copied
RUN echo "Contents of /src:" && ls -la

# Create output directory for packages
RUN mkdir -p /output

# Make build script executable
RUN chmod +x /src/build.sh

# Build OpenAuto using unified build script
COPY --from=aasdk / /aasdk
RUN if [ -f /aasdk/CMakeLists.txt ]; then AASDK_OPT="--aasdk-path /aasdk"; else AASDK_OPT="--with-aasdk"; fi; \
    /src/build.sh ${BUILD_TYPE} ${AASDK_OPT} --package --output-dir /output

# Default command
CMD ["bash", "-c", "echo 'OpenAuto build container ready. Packages are in /output/'"]

