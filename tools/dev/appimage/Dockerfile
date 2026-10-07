# check=skip=InvalidDefaultArgInFrom
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# MetOceanViewer AppImage build image: Ubuntu 22.04, so that everything
# compiled here needs at most glibc 2.35 (plan §6.24). GCC 14 comes from the
# ubuntu-toolchain-r/test PPA, because the code is C++23 (<expected>,
# <format>) and 22.04's own GCC is 12. Its binaries need a newer libstdc++
# than 22.04's (GLIBCXX_3.4.32 against 3.4.30), so the AppImage carries the
# PPA's libstdc++ and uses it only on systems whose own is older
# (packaging/linux/apprun-hooks/libstdcxx.sh.in, docs/packaging.md).
#
# Build and run it through tools/dev/run.sh with MOV_DEV_IMAGE=appimage, which
# passes every ARG below from tools/versions.env and vcpkg-configuration.json.
# Qt comes from the same aqtinstall prefix as the dev image's (~/Qt); Qt's
# Linux binaries need glibc 2.34. The AppImage is a CPack step of the
# package-linux preset (docs/packaging.md), which downloads linuxdeploy.

ARG APPIMAGE_UBUNTU_IMAGE
FROM ${APPIMAGE_UBUNTU_IMAGE}

ARG GCC_VERSION
ARG CMAKE_VERSION
ARG CMAKE_SHA256
ARG VCPKG_COMMIT

RUN for arg in GCC_VERSION CMAKE_VERSION CMAKE_SHA256 VCPKG_COMMIT; do \
      eval "test -n \"\${${arg}}\"" || { echo "build arg ${arg} is not set (use tools/dev/run.sh)" >&2; exit 1; }; \
    done

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# GCC from the toolchain PPA. It also upgrades libstdc++6 to GCC's, which is
# the copy the AppImage bundles.
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates gnupg software-properties-common \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update \
    && apt-get install -y --no-install-recommends "gcc-${GCC_VERSION}" "g++-${GCC_VERSION}" \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        autoconf-archive \
        automake \
        bison \
        ccache \
        curl \
        file \
        flex \
        git \
        libtool \
        make \
        ninja-build \
        pkg-config \
        python3 \
        python3-venv \
        tar \
        unzip \
        xz-utils \
        zip \
        # Qt 6 build/runtime libraries, as in the dev image (22.04 names),
        # plus what the platform plugins linuxdeploy bundles link (SM/ICE for
        # xcb, wayland for the wayland plugin): linuxdeploy fails on a
        # dependency it cannot resolve.
        libdbus-1-3 \
        libegl-dev \
        libegl1 \
        libfontconfig1 \
        libfreetype6 \
        libgl-dev \
        libgl1 \
        libgl1-mesa-dri \
        libglib2.0-0 \
        libice6 \
        libopengl-dev \
        libsm6 \
        libvulkan-dev \
        libwayland-client0 \
        libwayland-cursor0 \
        libwayland-egl1 \
        libx11-xcb1 \
        libxcb-cursor0 \
        libxcb-icccm4 \
        libxcb-image0 \
        libxcb-keysyms1 \
        libxcb-randr0 \
        libxcb-render-util0 \
        libxcb-shape0 \
        libxcb-xinerama0 \
        libxcb-xkb1 \
        libxkbcommon-dev \
        libxkbcommon-x11-0 \
        xauth \
        xvfb \
        # MapLibre Native (vcpkg port maplibre-native-qt) links the system zlib.
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/gcc gcc "/usr/bin/gcc-${GCC_VERSION}" 100 \
        --slave /usr/bin/g++ g++ "/usr/bin/g++-${GCC_VERSION}" \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 100 \
    && g++ --version | head -1

# The pinned upstream CMake, verified (22.04 ships 3.22).
RUN curl -fsSL -o /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
    && echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum --check --strict \
    && tar -xzf /tmp/cmake.tar.gz -C /opt \
    && rm /tmp/cmake.tar.gz \
    && ln -s "/opt/cmake-${CMAKE_VERSION}-linux-x86_64" /opt/cmake
ENV PATH=/opt/cmake/bin:$PATH

# aqtinstall (tools/dev/run.sh installs Qt with it) and the rest of the
# Python tooling.
COPY requirements.txt /opt/dev-requirements.txt
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir --upgrade pip \
    && /opt/venv/bin/pip install --no-cache-dir -r /opt/dev-requirements.txt
ENV PATH=/opt/venv/bin:$PATH

# vcpkg at the manifest baseline, as in the dev image.
ENV VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1
RUN git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && git -C "${VCPKG_ROOT}" checkout --quiet "${VCPKG_COMMIT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics \
    && chmod -R a+rwX "${VCPKG_ROOT}" \
    && git config --system --add safe.directory "${VCPKG_ROOT}"
ENV PATH=${VCPKG_ROOT}:$PATH

WORKDIR /work
CMD ["bash"]
