# check=skip=InvalidDefaultArgInFrom
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# MetOceanViewer AppImage build image: Ubuntu 22.04, so that everything
# compiled here needs at most glibc 2.35 (plan §6.24). The code is C++23
# (<expected>, <format>) and 22.04's own GCC is 12, so the first stage builds
# the GCC release APPIMAGE_GCC_VERSION from GNU's source tarball (pinned by
# SHA-256) with 22.04's GCC, into /opt/gcc. No PPA: its superseded packages
# expire, so a pinned set could not be rebuilt later. The binaries it makes
# need a newer libstdc++ than 22.04's (GLIBCXX_3.4.32 against 3.4.30); the
# AppImage carries a released libstdc++ of the same GCC version, fetched by
# packaging/linux/appimage.cmake (APPIMAGE_LIBSTDCXX_URL), and uses it only on
# systems whose own is older (docs/packaging.md).
#
# Build and run it through tools/dev/run.sh with MOV_DEV_IMAGE=appimage, which
# passes every ARG below from tools/versions.env and vcpkg-configuration.json.
# Qt comes from the same aqtinstall prefix as the dev image's (~/Qt); Qt's
# Linux binaries need glibc 2.34. The AppImage is a CPack step of the
# package-linux preset (docs/packaging.md), which downloads linuxdeploy.

ARG APPIMAGE_UBUNTU_IMAGE

# ---- GCC, built from the release tarball -------------------------------------
FROM ${APPIMAGE_UBUNTU_IMAGE} AS gcc

ARG APPIMAGE_GCC_VERSION
ARG APPIMAGE_GCC_SHA256

RUN for arg in APPIMAGE_GCC_VERSION APPIMAGE_GCC_SHA256; do \
      eval "test -n \"\${${arg}}\"" || { echo "build arg ${arg} is not set (use tools/dev/run.sh)" >&2; exit 1; }; \
    done

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl file g++ libgmp-dev libisl-dev libmpc-dev libmpfr-dev make xz-utils \
    && rm -rf /var/lib/apt/lists/*

# C and C++ only, no multilib, one stage (22.04's GCC 12 builds it); the
# sanitizer runtimes are not needed for packaging. configure detects Debian's
# multiarch layout (/usr/lib/x86_64-linux-gnu). Like Ubuntu's own GCC it makes
# PIE, the strong stack protector and a build id the defaults, and links with
# --as-needed (the specs edit): cmake/MapLibre.cmake's run-time layout relies
# on libraries listing only the libraries they use.
RUN curl -fsSL --retry 5 --retry-all-errors -o /tmp/gcc.tar.xz \
        "https://ftp.gnu.org/gnu/gcc/gcc-${APPIMAGE_GCC_VERSION}/gcc-${APPIMAGE_GCC_VERSION}.tar.xz" \
    && echo "${APPIMAGE_GCC_SHA256}  /tmp/gcc.tar.xz" | sha256sum --check --strict \
    && mkdir /tmp/gcc-src /tmp/gcc-build \
    && tar -xJf /tmp/gcc.tar.xz -C /tmp/gcc-src --strip-components=1 \
    && cd /tmp/gcc-build \
    && /tmp/gcc-src/configure --prefix=/opt/gcc \
        --enable-languages=c,c++ --disable-multilib --disable-bootstrap \
        --disable-libsanitizer --disable-nls --enable-default-pie \
        --enable-default-ssp --enable-linker-build-id \
        --with-pkgversion="MetOceanViewer AppImage build" \
    && make -j"$(nproc)" \
    && make install-strip \
    && rm -rf /tmp/gcc.tar.xz /tmp/gcc-src /tmp/gcc-build \
    && specs="$(dirname "$(/opt/gcc/bin/gcc -print-libgcc-file-name)")/specs" \
    && /opt/gcc/bin/gcc -dumpspecs > "${specs}" \
    && sed -i '/^\*link:$/{n;s/^/--as-needed /}' "${specs}" \
    && grep -A1 '^\*link:$' "${specs}" | grep -q -- '--as-needed'

# ---- The build image -----------------------------------------------------------
FROM ${APPIMAGE_UBUNTU_IMAGE}

ARG CMAKE_VERSION
ARG CMAKE_SHA256
ARG VCPKG_COMMIT

RUN for arg in CMAKE_VERSION CMAKE_SHA256 VCPKG_COMMIT; do \
      eval "test -n \"\${${arg}}\"" || { echo "build arg ${arg} is not set (use tools/dev/run.sh)" >&2; exit 1; }; \
    done

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        autoconf-archive \
        automake \
        binutils \
        bison \
        ca-certificates \
        ccache \
        curl \
        file \
        flex \
        git \
        libc6-dev \
        # What cc1 and cc1plus of /opt/gcc link.
        libgmp10 \
        libisl23 \
        libmpc3 \
        libmpfr6 \
        libtool \
        # de_DE.UTF-8 for the tests the package presets run (as the dev image).
        locales \
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

RUN locale-gen de_DE.UTF-8 \
    && locale -a | grep -qi '^de_DE\.utf-\?8$'

# The GCC of the first stage is the default cc/c++ (the presets name g++,
# vcpkg uses c++). Its libstdc++ comes first in the loader's cache, so the
# tests run against the runtime the code was compiled for.
COPY --from=gcc /opt/gcc /opt/gcc
RUN echo /opt/gcc/lib64 > /etc/ld.so.conf.d/00-gcc.conf \
    && ldconfig \
    && update-alternatives --install /usr/bin/gcc gcc /opt/gcc/bin/gcc 100 \
        --slave /usr/bin/g++ g++ /opt/gcc/bin/g++ \
        --slave /usr/bin/gcov gcov /opt/gcc/bin/gcov \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 100 \
    && g++ --version | head -1 \
    && ldconfig -p | grep -m1 'libstdc++.so.6 '

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
