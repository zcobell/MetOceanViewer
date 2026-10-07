# check=skip=InvalidDefaultArgInFrom
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# MetOceanViewer v5 development image. Mirrors the ubuntu-24.04 CI job:
# GCC 14 (default cc/c++), LLVM 20 tooling, CMake, Ninja, ccache, coverage
# tools, vcpkg at the manifest baseline, and the X11/EGL/Mesa runtime that Qt
# GUI tests need under Xvfb. Qt itself is NOT in the image: tools/dev/run.sh
# installs it with aqtinstall into a bind-mounted host directory (~/Qt).
#
# Build it through tools/dev/run.sh, which passes every ARG below from
# tools/versions.env and vcpkg-configuration.json and tags the image with a
# hash of its inputs. There are deliberately no defaults.

ARG UBUNTU_IMAGE
FROM ${UBUNTU_IMAGE}

ARG GCC_VERSION
ARG LLVM_VERSION
ARG LIBCXX_VERSION
ARG CMAKE_VERSION
ARG CMAKE_SHA256
ARG VCPKG_COMMIT

RUN for arg in GCC_VERSION LLVM_VERSION LIBCXX_VERSION CMAKE_VERSION CMAKE_SHA256 VCPKG_COMMIT; do \
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
        bison \
        ca-certificates \
        ccache \
        clang-${LIBCXX_VERSION} \
        clang-${LLVM_VERSION} \
        clang-format-${LLVM_VERSION} \
        clang-tidy-${LLVM_VERSION} \
        curl \
        flex \
        g++-${GCC_VERSION} \
        gcc-${GCC_VERSION} \
        git \
        lcov \
        libc++-${LIBCXX_VERSION}-dev \
        libc++abi-${LIBCXX_VERSION}-dev \
        libclang-rt-${LLVM_VERSION}-dev \
        libunwind-${LIBCXX_VERSION}-dev \
        libtool \
        # rsvg-convert renders the app icons (tools/make_icons.py).
        librsvg2-bin \
        lld-${LLVM_VERSION} \
        llvm-${LLVM_VERSION} \
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
        # Qt 6 build/runtime system libraries (OpenGL/EGL, xkb, xcb, fonts)
        # and the headless X server GUI tests run under (xvfb-run).
        libdbus-1-3 \
        libegl-dev \
        libegl1 \
        libfontconfig1 \
        libfreetype6 \
        libgl-dev \
        libgl1 \
        libgl1-mesa-dri \
        libglib2.0-0t64 \
        libopengl-dev \
        libvulkan-dev \
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

# de_DE.UTF-8 makes the locale-independence tests of mov::io non-vacuous: the
# decimal separator there is a comma, so a parser that honours the C locale
# misreads "1.5". The test asserts that the locale exists and fails if not.
RUN locale-gen de_DE.UTF-8 \
    && locale -a | grep -qi '^de_DE\.utf-\?8$'

# GCC is the default cc/c++/gcov; the presets name the compiler they want
# (g++ or clang++). Unversioned LLVM tool names point at LLVM_VERSION, and a
# missing target fails the build rather than leaving a dangling link.
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${GCC_VERSION} 100 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-${GCC_VERSION} \
        --slave /usr/bin/gcov gcov /usr/bin/gcov-${GCC_VERSION} \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 100 \
    && for tool in clang clang++ clang-tidy clang-format clang-apply-replacements \
                   llvm-cov llvm-profdata llvm-symbolizer ld.lld; do \
         target="/usr/lib/llvm-${LLVM_VERSION}/bin/${tool}"; \
         test -x "${target}" || { echo "missing ${target}" >&2; exit 1; }; \
         ln -sf "${target}" "/usr/local/bin/${tool}"; \
       done \
    # Clang + libc++ at LIBCXX_VERSION, for the dev-libcxx preset (macOS-like).
    && test -x "/usr/lib/llvm-${LIBCXX_VERSION}/bin/clang++" \
    && ln -sf "/usr/lib/llvm-${LIBCXX_VERSION}/bin/clang++" /usr/local/bin/clang++-libcxx

# Ubuntu 24.04 ships CMake 3.28; use the pinned upstream release, verified.
RUN curl -fsSL -o /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
    && echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum --check --strict \
    && tar -xzf /tmp/cmake.tar.gz -C /opt \
    && rm /tmp/cmake.tar.gz \
    && ln -s "/opt/cmake-${CMAKE_VERSION}-linux-x86_64" /opt/cmake
ENV PATH=/opt/cmake/bin:$PATH

# Python tooling in a venv (PEP 668 forbids system pip installs on noble).
COPY requirements.txt /opt/dev-requirements.txt
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir -r /opt/dev-requirements.txt
ENV PATH=/opt/venv/bin:$PATH

# vcpkg at the manifest baseline. Manifest mode still writes buildtrees/ and
# packages/ under VCPKG_ROOT, and run.sh runs as the host uid, so the tree is
# world-writable. Downloads and binary archives go to a persistent host cache.
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
