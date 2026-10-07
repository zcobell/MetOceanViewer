# Portability notes (macOS libc++, Windows MSVC)

Fold into `docs/core-design.md` §1 (toolchain), then delete this file.

## Reproduction

The `dev-libcxx` preset (Clang 18 + libc++ 18, Ubuntu noble packages; vcpkg
C++ ports through the `x64-linux-libcxx` overlay triplet) fails exactly where
the macOS CI annotations pointed, on the unmodified tree at `1b80fb8e`:

- `test_units_constexpr.cpp:371`: constexpr variable cannot have non-literal
  type `const Unit` (`variant<..., OtherUnit>`).
- `test_units_constexpr.cpp:294-299`: `is_temperature(Unit{...})` is not an
  integral constant expression.
- `test_toolchain_probe.cpp:95-96`: no `std::stop_source` / `std::stop_token`.

and in addition, none of which CI had reached yet:

- `test_quantity_constexpr.cpp:170-176, 200`: the same variant problem for
  `QuantityId` (`variant<GenericQuantity, Quantity>`).
- `test_sample_constexpr.cpp`: the test helper `finite()` is ambiguous with
  glibc's `::finite(double)` that libc++'s `<math.h>` exposes (renamed
  `sample_of`).
- `hwm_fixture.hpp:31`: floating-point `std::from_chars` is a deleted overload
  in libc++ (no `__cpp_lib_to_chars`).
- `core_compile_fail_checks`: the harness ran the compiler without the build's
  `-stdlib=libc++`, so on macOS it would also have lacked the SDK flags.

Nothing in `src/core` needed changing: the production constexpr uses
(`StationKey`/`StationId::make`, the registries, `Sample`) evaluate fine with
libc++ 18; only tests that put a `std::variant` holding a `std::string` into a
constant expression fail.

## Feature table (probed with `<version>` macros, `-std=c++23`)

| Feature | libc++ 18 | libc++ 19 | libc++ 20 | Consequence |
|---|---|---|---|---|
| `__cpp_lib_variant` | 202102 | 202102 | 202106 | gate for constexpr `variant<string>` |
| constexpr `variant` holding `std::string` (tested) | no | yes | yes | libc++ 19 works despite the macro; the gate stays on the macro (safe) |
| `__cpp_lib_constexpr_string` | 201907 | 201907 | 201907 | useless as a gate |
| `__cpp_lib_jthread` / `std::stop_token` | experimental only | experimental only | yes | probe SKIPs |
| `__cpp_lib_ranges_fold`, `_enumerate`, `_join_with`, `_slide` | no | no | no | not used; probes SKIP |
| `views::zip`, `views::chunk_by` | yes | yes | yes | `zip` has no macro in libc++ |
| `__cpp_lib_to_chars` (floating) | no | no | no | `strtod` fallback (io: WP5) |
| `__cpp_lib_format` | no | 202110 | 202110 | `{:%FT%T}` of `sys_time` compiles in 18 anyway |
| `__cpp_lib_constexpr_charconv`, `__cpp_lib_expected` | yes | yes | yes | |

libc++ 17 could not be installed beside 18 (package conflict) and was not
measured. Apple's libc++ is a fork: Xcode 16.0-16.2 (Apple Clang 16) is
believed to track LLVM 17-18, Xcode 16.3-16.4 and 26.0 (Apple Clang 17) LLVM 19.
That mapping is from memory and cannot be checked from Linux.

libc++ availability annotations (`__configuration/availability.h`): with
`CMAKE_OSX_DEPLOYMENT_TARGET=14.0`, floating `to_chars`/`from_chars` (macOS
13.4), `std::pmr` (14.0) and verbose abort are available. `std::print`,
`std::chrono::tzdb`/`zoned_time`/`current_zone` and the key functions of
`bad_expected_access`/`bad_function_call` are marked unavailable or
inline-only for LLVM 18-19 on Apple targets. The core uses none of them;
keep `sys_time` only and avoid `std::print`.

## Fixes

- `tests/support/include/mov/test/toolchain.hpp`: `MOV_TEST_CONSTEXPR_VARIANT`
  (`__cpp_lib_variant >= 202106L`), `MOV_CONSTEXPR_VARIANT` (`constexpr` or
  `const`) and `MOV_STATIC_REQUIRE_VARIANT` / `_FALSE_VARIANT` (`STATIC_REQUIRE`
  or the same check at run time). Used for the `Unit` and `QuantityId` checks.
  Linux GCC and libstdc++ keep every compile-time check; so does libc++ 20.
- Probes: `std::stop_token` is gated on `__cpp_lib_jthread` and SKIPs. The
  probe also had a real bug that libstdc++ hid: `request_stop()` is not const.
  `std::isfinite` constexpr is detected, not required (MSVC STL).
- `hwm_fixture.hpp` `parse_number`: `from_chars` when `__cpp_lib_to_chars`,
  else `strtod` (the tests run in the "C" locale).
- `core_compile_fail_checks` passes `CMAKE_CXX_FLAGS` and `-isysroot` to the
  compiler it runs by hand.
- `test_time.cpp`: `std::getenv` is C4996 under MSVC (an error with `/WX`);
  Windows uses `_dupenv_s`.

Production constexpr code is untouched. The runtime half of each gated check
still runs on libc++ 18/19 (and the relaxed constexpr executables run the other
checks at run time too).

## MSVC (no compiler available locally)

Verified by reasoning and by emulation on Linux:

- `long double` is `double` on MSVC: `test_hwm_stats.cpp` with every
  `long double` replaced by `double` still passes.
- Fixtures are `-text` in `.gitattributes`, so the Windows checkout does not
  turn them into CRLF; `golden.py --check` reads with universal newlines.
- No `__builtin`, `__attribute__`, `ssize_t`, POSIX headers or non-ASCII
  literals in `src/core` or `tests/core`; `TZ` handling in `test_time.cpp`
  already had a `_WIN32` branch.

Not verifiable here, so the first Windows run decides:

- `/W4 /WX` diagnostics that GCC and Clang do not have (C4702 unreachable code
  after full-coverage switches, C4458, C4996 elsewhere).
- `core_compile_fail_checks` stays skipped on MSVC (GCC/Clang flag syntax;
  adding `/Zs /W4 /WX` without being able to run `cl` risks a red job).
- The `template <double V>` + `requires` detection in the constexpr probe.
- Probes for `views::zip`, `chunk_by`, `from_chars` on MSVC STL.

The Windows job runs `ctest --label-exclude gui`: every test here carries no
`gui` label, so all of them run.

## Runner images (checked 2026-10-07, actions/runner-images)

| Label | Xcode | Apple Clang | Status |
|---|---|---|---|
| `macos-14` (arm64) | 15.0.1-15.4 (default 15.4), 16.1, 16.2 | 15 (default) | deprecation began 2026-07-06, unsupported 2026-11-02 |
| `macos-15` (arm64) | 16.0-16.4 (default 16.4), 26.0.1-26.3 | 17.0.0 (default) | GA |
| `macos-26` (arm64, `macos-latest`) | 26.0.1-26.6 (default 26.6) | 21.0.0 | GA |

Homebrew LLVM (18.1.8 on macos-15, 20.1.8 on macos-26) is installed too but
would need its own libc++ dylib at run time; not considered.

Recommendation: leave `macos-14` now (it disappears in under four weeks) for
`macos-15`, pinning Xcode 16.2. That is the Apple Clang 16 the job already
used on macos-14 (`setup-xcode` with `'16'` resolved to 16.2), so the libc++
that produced the CI errors, and that the fixes target, stays the one under
test; it is also the oldest toolchain we support. `macos-15` has 16.0 to 16.4,
so the pin can move to 16.4 (Apple Clang 17, libc++ believed to be LLVM 19,
where constexpr `variant<string>` already works but `stop_token` still needs
`-fexperimental-library`) without code changes. Xcode 26 would hide gaps behind
a newer libc++ without removing the need for the gates; `fold_left`,
`enumerate` and floating `from_chars` are missing through libc++ 20 and the
code does not use them. The deployment target stays 14.0: every gap is
header-only, and the availability table above shows no feature we use that
needs a newer OS. The `ci.yml` change is two lines and easy to revert.

## CI

- `libcxx` job (blocking): `cmake --workflow --preset dev-libcxx` on
  ubuntu-24.04 with clang and libc++ `LIBCXX_VERSION` (18). Debug, ~one
  build; deterministic, so it is blocking. It is the only place the libc++
  gaps show up before the macOS job.
- The libc++ 19 and 20 trees were also syntax-checked against the same
  sources by hand (all TUs of the build, `-fsyntax-only`): clean, also with
  `-fexperimental-library`.
