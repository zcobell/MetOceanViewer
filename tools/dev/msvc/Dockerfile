# check=skip=InvalidDefaultArgInFrom
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# MetOceanViewer v5 Windows/MSVC cross-check image (MOV_DEV_IMAGE=msvc): clang-cl,
# lld-link, llvm-rc and llvm-mt from LLVM_VERSION, the MSVC STL/CRT and the
# Windows SDK splatted by xwin (x86_64 only, release CRT only), CMake, Ninja,
# vcpkg, and Wine to run the cross-built test executables. Compiles first-party
# code against the same MSVC STL headers as the windows-2022 job; it is NOT
# MSVC's front end. See tools/dev/README.md ("Windows/MSVC cross-check").
#
# Build it through tools/dev/run.sh (MOV_DEV_IMAGE=msvc), which passes every ARG
# below from tools/versions.env and vcpkg-configuration.json. There are
# deliberately no defaults. The build downloads about 85 MiB of Microsoft CRT
# and SDK packages (xwin accepts the Microsoft license: --accept-license, which
# the repository owner has done) and needs network access.

ARG UBUNTU_IMAGE
FROM ${UBUNTU_IMAGE}

ARG LLVM_VERSION
ARG CMAKE_VERSION
ARG CMAKE_SHA256
ARG VCPKG_COMMIT
ARG XWIN_VERSION
ARG XWIN_SHA256
ARG XWIN_CRT_VERSION
ARG XWIN_SDK_VERSION

RUN for arg in LLVM_VERSION CMAKE_VERSION CMAKE_SHA256 VCPKG_COMMIT XWIN_VERSION XWIN_SHA256 \
                XWIN_CRT_VERSION XWIN_SDK_VERSION; do \
      eval "test -n \"\${${arg}}\"" || { echo "build arg ${arg} is not set (use tools/dev/run.sh)" >&2; exit 1; }; \
    done

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# gcc/g++/make/perl/pkg-config: vcpkg builds the host tools its Windows ports
# need (sqlite3 for PROJ's database) natively. wine64 + libwine: runs the
# cross-built tests.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        ccache \
        clang-${LLVM_VERSION} \
        curl \
        g++ \
        gcc \
        git \
        libwine \
        lld-${LLVM_VERSION} \
        llvm-${LLVM_VERSION} \
        make \
        ninja-build \
        perl \
        pkg-config \
        python3 \
        tar \
        unzip \
        wine64 \
        xz-utils \
        zip \
    && rm -rf /var/lib/apt/lists/*

# The unversioned names the toolchain file uses. clang-cl is clang run under
# that name (Ubuntu ships the symlink in clang-tools, which is not installed).
# A missing target fails the build rather than leaving a dangling link.
RUN ln -s clang "/usr/lib/llvm-${LLVM_VERSION}/bin/clang-cl" \
    && for tool in clang clang-cl lld-link llvm-rc llvm-mt llvm-lib llvm-ml llvm-objdump llvm-readobj; do \
      target="/usr/lib/llvm-${LLVM_VERSION}/bin/${tool}"; \
      test -x "${target}" || { echo "missing ${target}" >&2; exit 1; }; \
      ln -sf "${target}" "/usr/local/bin/${tool}"; \
    done \
    && ln -sf /usr/lib/wine/wine64 /usr/local/bin/wine64

# Ubuntu 24.04 ships CMake 3.28; use the pinned upstream release, verified.
RUN curl -fsSL -o /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
    && echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum --check --strict \
    && tar -xzf /tmp/cmake.tar.gz -C /opt \
    && rm /tmp/cmake.tar.gz \
    && ln -s "/opt/cmake-${CMAKE_VERSION}-linux-x86_64" /opt/cmake
ENV PATH=/opt/cmake/bin:$PATH

# xwin downloads the MSVC CRT and the Windows SDK from Microsoft's Visual Studio
# manifest (hash-checked by xwin) and lays them out for a case-sensitive file
# system (symlinks for the SDK's mixed-case headers and libraries). The versions
# are pinned; the download cache is removed in the same layer.
ENV XWIN_ROOT=/opt/xwin
RUN curl -fsSL -o /tmp/xwin.tar.gz \
        "https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-x86_64-unknown-linux-musl.tar.gz" \
    && echo "${XWIN_SHA256}  /tmp/xwin.tar.gz" | sha256sum --check --strict \
    && mkdir /tmp/xwin \
    && tar -xzf /tmp/xwin.tar.gz -C /tmp/xwin --strip-components=1 \
    && /tmp/xwin/xwin --accept-license --cache-dir /tmp/xwin-cache --arch x86_64 \
        --crt-version "${XWIN_CRT_VERSION}" --sdk-version "${XWIN_SDK_VERSION}" \
        splat --output "${XWIN_ROOT}" \
    && test -f "${XWIN_ROOT}/crt/include/vector" \
    && test -f "${XWIN_ROOT}/sdk/include/um/Windows.h" \
    && test -f "${XWIN_ROOT}/sdk/lib/um/x86_64/kernel32.lib" \
    # C++/WinRT and WinRT headers are not used by any target here.
    && rm -rf "${XWIN_ROOT}/sdk/include/cppwinrt" "${XWIN_ROOT}/sdk/include/winrt" \
    && rm -rf /tmp/xwin /tmp/xwin-cache /tmp/xwin.tar.gz

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

# Wine: no Mono/Gecko download prompts; the prefix lives under $HOME (run.sh
# points it at the persistent host cache).
ENV WINEDEBUG=-all \
    WINEDLLOVERRIDES="mscoree,mshtml="

WORKDIR /work
CMD ["bash"]
