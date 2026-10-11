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

# Images for OpenHU (docker/dev.sh uses the dev target):
#   --target dev       build dependencies + runtime tools; sources, builds and aasdk stay outside in volumes
#   --target packages  builds OpenHU and exports its .deb together with the aasdk packages it was built against:
#                      docker buildx build --target packages --output type=local,dest=packages \
#                          --build-context aasdk=<dir> .
# The aasdk context is either a directory with libaasdk*.deb (CI: scripts/fetch-aasdk.sh) or an aasdk
# source tree (../aasdk-ref), which is packaged first. Without it the branch openhu is cloned from GitHub.

ARG DEBIAN_VERSION=trixie

# Optional aasdk packages or source tree, empty by default
FROM scratch AS aasdk

FROM debian:${DEBIAN_VERSION}-slim AS deps
ARG DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8

# Downloaded packages stay in the BuildKit cache instead of being fetched again for every image build
RUN rm -f /etc/apt/apt.conf.d/docker-clean \
    && echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' > /etc/apt/apt.conf.d/keep-cache

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
        # build tools (lsb-release: DebPackageFilename.cmake)
        build-essential \
        cmake \
        ninja-build \
        ccache \
        pkg-config \
        git \
        lsb-release \
        ca-certificates \
        # aasdk and OpenHU
        libboost-system-dev \
        libboost-log-dev \
        libprotobuf-dev \
        protobuf-compiler \
        libusb-1.0-0-dev \
        libssl-dev \
        libgps-dev \
        librtaudio-dev \
        qt6-base-dev \
        qt6-declarative-dev \
        qt6-multimedia-dev \
        qt6-websockets-dev \
        # qmllint, and the QML modules of the shell it checks the imports against (also needed at runtime)
        qt6-declarative-dev-tools \
        qml6-module-qtquick \
        qml6-module-qtquick-window \
        qml6-module-qtquick-layouts \
        qml6-module-qtquick-controls \
        qml6-module-qtmultimedia \
        # SVG icons of the plugins
        qt6-svg-plugins \
        # H.264 decoder of the Android Auto video (same FFmpeg as Qt Multimedia)
        libavcodec-dev \
        libavutil-dev \
        libswscale-dev \
        # packaging
        file \
        dpkg-dev \
        fakeroot

# Sources are mounted from the host and owned by another user
RUN git config --system --add safe.directory '*'

ENV CMAKE_GENERATOR=Ninja \
    CMAKE_C_COMPILER_LAUNCHER=ccache \
    CMAKE_CXX_COMPILER_LAUNCHER=ccache \
    CCACHE_DIR=/ccache

# Development image: build in volumes (docker/dev.sh build) and run from there (docker/dev.sh run)
FROM deps AS dev
ARG DEBIAN_FRONTEND=noninteractive

# Runtime packages of the .deb (CPACK_DEBIAN_PACKAGE_DEPENDS) and tools for the container:
# Xephyr provides the virtual car display (a window on the host), NetworkManager/iw/BlueZ of the host are used via D-Bus
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
        qt6-wayland \
        pulseaudio-utils \
        usbutils \
        fonts-dejavu-core \
        xserver-xephyr \
        can-utils \
        network-manager \
        iw \
        python3 \
        python3-websockets \
        rtl-sdr \
        sox \
        x11-apps \
        xdotool \
        imagemagick

# aasdk is installed to /opt/aasdk (volume), OpenHU runs from its build directory (volume)
ENV CMAKE_PREFIX_PATH=/opt/aasdk \
    CPATH=/opt/aasdk/include \
    LIBRARY_PATH=/opt/aasdk/lib \
    LD_LIBRARY_PATH=/opt/aasdk/lib \
    PATH=/build/openhu/bin:$PATH

COPY docker/entrypoint.sh /usr/local/bin/openhu-entrypoint

# openhu.ini & co. are written to the working directory -> mounted as a volume
WORKDIR /config

# Complete build for CI and releases
FROM deps AS aasdk-build
ARG AASDK_REPO=https://github.com/average-web-dev/aasdk.git
ARG AASDK_BRANCH=openhu
RUN --mount=type=bind,from=aasdk,target=/aasdk-src,rw \
    --mount=type=cache,target=/ccache \
    mkdir -p /aasdk-debs \
    && if ls /aasdk-src/libaasdk*.deb >/dev/null 2>&1; then cp /aasdk-src/libaasdk*.deb /aasdk-debs/; \
    else \
        if [ -f /aasdk-src/CMakeLists.txt ]; then src=/aasdk-src; \
        else git clone --depth 1 --branch "$AASDK_BRANCH" "$AASDK_REPO" /tmp/aasdk && src=/tmp/aasdk; fi \
        && cd "$src" \
        && BUILD_DIR=/tmp/aasdk-build BUILD_TESTS=OFF \
           CMAKE_EXTRA_ARGS="-DCPACK_DEBIAN_PACKAGE_RELEASE=$(bash scripts/distro_release.sh)" \
           bash ./build.sh release package --skip-protobuf --skip-absl \
        && cp /tmp/aasdk-build/packages/libaasdk*.deb /aasdk-debs/ \
        && rm -rf /tmp/aasdk /tmp/aasdk-build; \
    fi \
    && dpkg -i /aasdk-debs/*.deb \
    && dpkg-query -W libaasdk libaasdk-dev

FROM aasdk-build AS openhu-build
WORKDIR /src
COPY CMakeLists.txt LICENSE ./
COPY cmake_modules cmake_modules
COPY include include
COPY src src
COPY assets assets
COPY packaging packaging
COPY scripts scripts
COPY tests tests
# qmllint of the shell QML, build, unit tests, package
RUN --mount=type=cache,target=/ccache \
    bash tests/qmllint.sh \
    && cmake -S /src -B /tmp/build -DCMAKE_BUILD_TYPE=Release \
        -DCPACK_DEBIAN_PACKAGE_RELEASE="$(bash scripts/distro_release.sh)" \
        -DCPACK_PROJECT_CONFIG_FILE=/src/cmake_modules/CPackProjectConfig.cmake \
    && cmake --build /tmp/build \
    && ctest --test-dir /tmp/build --output-on-failure \
    && cd /tmp/build && cpack -G DEB \
    && mkdir /out && cp -v /tmp/build/*.deb /aasdk-debs/*.deb /out/

FROM scratch AS packages
COPY --from=openhu-build /out/ /
