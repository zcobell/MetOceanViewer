# Development environment

The v5 build needs GCC 14 or Clang 19+ (`<format>`, `<expected>`), which the
Debian 12 host does not have, so builds run in an Ubuntu 24.04 image that
mirrors the `ubuntu-24.04` CI jobs.

| Tool | Version |
|---|---|
| GCC (default `cc`/`c++`) | `GCC_VERSION` in `tools/versions.env` |
| clang, clang-tidy, clang-format, llvm-cov, libFuzzer/sanitizer runtimes | `LLVM_VERSION` |
| CMake (upstream release, SHA-256 verified) | `CMAKE_VERSION` |
| vcpkg | the `baseline` in `vcpkg-configuration.json` |
| Ninja, ccache, lcov; gcovr, pre-commit, aqtinstall, lizard (`requirements.txt`) | |
| Xvfb, Mesa, xcb/xkb/EGL libraries | for Qt GUI tests |

## Usage

```sh
tools/dev/run.sh cmake --workflow --preset dev   # one command in the container
tools/dev/run.sh                                 # interactive shell
MOV_DEV_QT=1 tools/dev/run.sh                    # shell, with Qt installed first
```

`run.sh` runs the command as your uid/gid with the repository mounted at its
host path, so build trees and `compile_commands.json` paths match on both
sides. The compiler is chosen by the preset (`g++` or `clang++`), never by
`CC`/`CXX`.

In a git worktree (`.git` is a file), `run.sh` also mounts the main
repository's git directory read-only and the worktree's own gitdir read-write,
at their host paths, so `git` and `pre-commit` work in the container.

The image is tagged `metoceanviewer-dev:<hash>`, a hash of the Dockerfile,
`requirements.txt`, `tools/versions.env` and the vcpkg baseline; `run.sh`
rebuilds it (about 3 minutes) whenever one of them changes, and also tags it
`latest`. Old tags can be removed with `docker image prune`.

Qt (`QT_VERSION`, `QT_MODULES`) is installed with aqtinstall on demand: when
`MOV_DEV_QT=1` or an argument names a preset ending in `-qt` (`dev-qt`). A
stamp file in the Qt prefix records version, arch and modules; a change in
`tools/versions.env` reinstalls, and a lock serializes concurrent installs.
`run.sh` exports `QT_ROOT_DIR`, which the Qt presets put on `CMAKE_PREFIX_PATH`.

Persistent state stays on the host:

| Host path | Contents |
|---|---|
| `~/Qt/<version>/gcc_64` | Qt |
| `~/.cache/metoceanviewer-dev/ccache` | ccache |
| `~/.cache/metoceanviewer-dev/vcpkg` | vcpkg downloads and binary cache |
| `~/.cache/metoceanviewer-dev/home` | `$HOME` in the container (pre-commit and pip caches) |

Overrides: `MOV_QT_ROOT`, `MOV_DEV_CACHE`, and `MOV_DOCKER_ARGS` for extra
`docker run` arguments.

## Locales

The image generates `de_DE.UTF-8`. The locale-independence tests of `mov::io`
(`parse_double` under a comma-decimal locale) assert that the locale exists
and fail, rather than skip, when it does not: every preset sets
`MOV_REQUIRE_LOCALES=ON`, and CI generates the locale on the Linux jobs
(`.github/actions/setup-locales`). On a native machine without it, either
`locale-gen de_DE.UTF-8` (Debian/Ubuntu) or configure with
`-DMOV_REQUIRE_LOCALES=OFF` to turn the failure into a skip.

## GUI tests

GUI tests run under Xvfb (`xvfb-run -a ctest ...`), not Qt's `offscreen`
platform, because MapLibre renders through OpenGL and needs a GL context; Mesa
provides one in software.

## Native build (without Docker)

Any machine with a C++23 toolchain works the same way the CI jobs do:

1. A compiler with `<format>` and `<expected>`: GCC 14+, Clang 19+ with
   libstdc++ 14 or libc++ 18+, Xcode 16+, or Visual Studio 2022 17.10+.
2. CMake `CMAKE_VERSION` or newer and Ninja (e.g. `pip install cmake ninja`).
3. vcpkg checked out at the baseline commit and bootstrapped, with `VCPKG_ROOT`
   pointing at it:
   `git clone https://github.com/microsoft/vcpkg && git -C vcpkg checkout <baseline> && vcpkg/bootstrap-vcpkg.sh`.
4. For the Qt presets, Qt `QT_VERSION` (aqtinstall: `aqt install-qt <host> desktop <version> <arch> -m <QT_MODULES>`)
   and `QT_ROOT_DIR` set to its prefix.
5. `pip install -r tools/dev/requirements.txt` for gcovr, pre-commit and lizard.

Then `cmake --workflow --preset dev` (the Linux presets name `g++`/`clang++`;
on macOS and Windows use `ci-macos` / `ci-windows`, the latter from a Visual
Studio developer prompt).

## Versions and what is still synced by hand

`tools/versions.env` is read by `run.sh` (image build args) and by CI
(`.github/actions/load-versions`); the vcpkg baseline is read from
`vcpkg-configuration.json` by both; CI installs Python tools from
`requirements.txt`. These still have to be changed by hand:

- `rev:` of each hook in `.pre-commit-config.yaml` (`pre-commit autoupdate`).
  The `mirrors-clang-format` major must equal `LLVM_VERSION`, and the lizard
  pin in its hooks must equal `requirements.txt`.
- The `lukka/get-cmake` action pin in `.github/workflows/ci.yml` (Dependabot
  updates the SHA; the CMake version itself comes from `CMAKE_VERSION`).
- `CMAKE_SHA256` whenever `CMAKE_VERSION` changes (from Kitware's
  `cmake-<version>-SHA-256.txt`), and `UBUNTU_IMAGE`'s digest when moving the
  base image.
- `cmake_minimum_required` in `CMakeLists.txt` and `cmakeMinimumRequired` in
  `CMakePresets.json` follow `CMAKE_VERSION` (the only version CI exercises).
- `actions/*` versions in the workflow and composite actions (Dependabot).
