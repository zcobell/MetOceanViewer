// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The netCDF-C wrapper (core-design.md section 4). Every netCDF-C call of
// mov::io is made through it; nothing outside src/io/netcdf/ includes
// <netcdf.h>.
//
// THREADING: netCDF-C (and the HDF5 under it) is not thread-safe, and this
// wrapper adds no lock. The caller must serialize all calls into mov::io
// netCDF functions (every member of File, write_netcdf_atomic and every
// reader built on them), across all files: the providers and the app run
// them on one serial queue (C11). Debug builds assert on concurrent or
// re-entrant entry.
//
// Blocking I/O: call these from a worker, never the GUI thread.

#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/masking.hpp"
#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/text_file.hpp"

namespace mov::io::nc {

class File;

namespace detail {

/// What write_netcdf_atomic runs on the new file.
using AtomicNcBody = std::function<std::expected<void, Error>(File&)>;

/// write_netcdf_atomic with its stages exposed to a FaultInjector (tests).
[[nodiscard]] std::expected<void, Error> write_netcdf_atomic_impl(
    const std::filesystem::path& target, const AtomicNcBody& body,
    const io::detail::FaultInjector& fault = {});

}  // namespace detail

/// An open netCDF file. Move-only; a moved-from or closed File answers every
/// call with WrapperFault::closed.
///
/// Errors: a netCDF-C status is LibraryStatus (positive codes are errno
/// values, as in netCDF-C); what the wrapper refuses itself is a
/// WrapperFault. NcError::object names the variable or dimension, or an
/// attribute as `var:att` (`:att` for a global one), in ncdump's notation;
/// NcError::file is the path given to open, or the target of an atomic write.
///
/// Close policy: a read handle's destructor closes the file and ignores a
/// close error. close() reports one. A write handle (only the atomic writer
/// has one) is closed by the writer; destroyed open, it is aborted
/// (nc_abort). A close that fails is followed by nc_abort, which always
/// releases the netCDF id, so no id leaks whatever happens.
class File {
 public:
  /// Opens `path` read-only. Only a regular file is opened (a directory is
  /// EISDIR, anything else EINVAL, both as LibraryStatus) so a FIFO cannot
  /// block. `limits` caps attribute sizes; the data reads take their own.
  [[nodiscard]] static std::expected<File, NcError> open(
      const std::filesystem::path& path, const ReadLimits& limits);

  File(File&& other) noexcept;
  File& operator=(File&& other) noexcept;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File();

  /// Closes the file and reports a close error. The handle is released
  /// either way.
  [[nodiscard]] std::expected<void, NcError> close() &&;

  [[nodiscard]] bool is_open() const noexcept { return ncid_.has_value(); }
  /// The path errors name: the opened file, or an atomic write's target.
  [[nodiscard]] const std::filesystem::path& path() const& noexcept {
    return path_;
  }
  const std::filesystem::path& path() const&& = delete;

  // ---- structure ----------------------------------------------------------
  // An absent dimension or variable is nullopt, never id 0 (B12).

  [[nodiscard]] std::expected<std::optional<DimInfo>, NcError> find_dim(
      NcNameRef name) const;
  [[nodiscard]] std::expected<std::optional<VarInfo>, NcError> find_var(
      NcNameRef name) const;
  /// Every variable of the root group, in id order.
  [[nodiscard]] std::expected<std::vector<VarInfo>, NcError> variables() const;

  // ---- attributes ----------------------------------------------------------
  // An absent attribute is nullopt; an absent variable is an error.

  /// An NC_CHAR attribute (all attlen bytes, NULs included, B8) or a
  /// one-element NC_STRING attribute (a NULL string is ""). Longer than the
  /// open limits' max_att_bytes is `too_large`; any other type is
  /// `type_mismatch`, an NC_STRING attribute of several strings
  /// `count_mismatch`.
  [[nodiscard]] std::expected<std::optional<std::string>, NcError> text_att(
      AttTarget on, NcNameRef name) const;

  /// A numeric attribute whose type is exactly T's (`type_mismatch`
  /// otherwise: an EPSG code stored as text is not a number, B10).
  template <Numeric T>
  [[nodiscard]] std::expected<std::optional<std::vector<T>>, NcError>
  numeric_att(AttTarget on, NcNameRef name) const;

  // ---- data ----------------------------------------------------------------
  // Data reads take a ReadContext: the element count of the request is
  // checked against limits.max_elements before anything is allocated
  // (`too_large`; `overflow` if it does not even fit in size_t), the slab
  // must lie inside the dimensions (NC_EINVALCOORDS, NC_EEDGE), and the read
  // is done in blocks of at most limits.slab_elements, polling ctx.stop
  // before each (Cancelled). Errors are therefore io::Error.

  /// The values of `slab` of variable `name`, converted to T only where no
  /// value can change (readable_as, section 4.3): a float variable reads as
  /// float or double, never a double variable as float (B4).
  template <Numeric T>
  [[nodiscard]] std::expected<std::vector<T>, Error> read(
      NcNameRef name, const Slab& slab, const ReadContext& ctx) const;

  /// The missing-data attributes of variable `name`, whose type must be exactly
  /// T's. _FillValue, missing_value and valid_* of another type are
  /// `type_mismatch`; a _FillValue of more than one value, or a valid_range
  /// of other than two, `count_mismatch`; scale_factor and add_offset must be
  /// one float or double. `_Unsigned = "true"` is `unsupported_unsigned`.
  template <Numeric T>
  [[nodiscard]] std::expected<Masking<T>, NcError> masking(
      NcNameRef name) const;

  /// read, masked and unpacked in the variable's own type before widening
  /// (B4, B9): byte, short, int, float and double variables. 64-bit integer
  /// and every other type are `type_mismatch`.
  [[nodiscard]] std::expected<std::vector<core::Sample>, Error> read_samples(
      NcNameRef name, const Slab& slab, const ReadContext& ctx) const;

  /// The rows of a 2-D NC_CHAR variable, each exactly as long as the second
  /// dimension (the stride comes from the file, B7, B11), raw: NULs and the
  /// bytes after them are kept; the caller applies its policy (C14). Another
  /// rank is `rank_mismatch`, another type `type_mismatch`.
  [[nodiscard]] std::expected<std::vector<std::string>, Error> read_char_rows(
      NcNameRef name, const ReadContext& ctx) const;

  /// Every element of an NC_STRING variable, in row-major order; a NULL
  /// element is "". The library's strings are always freed (B21). Their
  /// total length is capped by limits.max_text_bytes.
  [[nodiscard]] std::expected<std::vector<std::string>, Error> read_strings(
      NcNameRef name, const ReadContext& ctx) const;

  // ---- writing: only on the File handed to write_netcdf_atomic's body ------

  /// A dimension of `length` > 0 (0 would make it unlimited:
  /// `zero_length_dim`).
  [[nodiscard]] std::expected<DimInfo, NcError> define_dim(NcNameRef name,
                                                           std::size_t length);
  template <Numeric T>
  [[nodiscard]] std::expected<VarInfo, NcError> define_var(
      NcNameRef name, std::span<const DimInfo> dims, const VarOptions<T>& opt);
  [[nodiscard]] std::expected<VarInfo, NcError> define_char_var(
      NcNameRef name, std::span<const DimInfo> dims);
  /// An NC_CHAR attribute of exactly text.size() bytes (B15); longer than
  /// max_att_bytes is `too_large`.
  [[nodiscard]] std::expected<void, NcError> put_att(AttTarget on,
                                                     NcNameRef name,
                                                     std::string_view text);
  template <Numeric T>
  [[nodiscard]] std::expected<void, NcError> put_att(AttTarget on,
                                                     NcNameRef name,
                                                     std::span<const T> values);
  /// Writes `data` to `slab` of variable `name`, whose type must be exactly T's
  /// (`type_mismatch`); data.size() must equal the slab's element count
  /// (`count_mismatch`).
  template <Numeric T>
  [[nodiscard]] std::expected<void, NcError> put(NcNameRef name,
                                                 std::span<const T> data,
                                                 const Slab& slab);
  /// Writes one row per element of the first dimension of a 2-D NC_CHAR
  /// variable, NUL-padded to the second (B15). rows.size() must equal the
  /// first dimension (`count_mismatch`); a longer row is `name_too_long`.
  [[nodiscard]] std::expected<void, NcError> put_char_rows(
      NcNameRef name, std::span<const std::string> rows);
  [[nodiscard]] std::expected<void, NcError> end_define();

 private:
  friend std::expected<void, Error> detail::write_netcdf_atomic_impl(
      const std::filesystem::path& target, const detail::AtomicNcBody& body,
      const io::detail::FaultInjector& fault);

  struct AttShape {
    Type type;
    std::size_t length;
  };

  File(int ncid, bool writable, std::filesystem::path path,
       const ReadLimits& limits) noexcept;

  /// NC_NETCDF4 | NC_NOCLOBBER at `temp`; errors and NcError::file name
  /// `target`.
  [[nodiscard]] static std::expected<File, NcError> create(
      const std::filesystem::path& temp, const std::filesystem::path& target);

  /// nc_abort, ignoring its status.
  void abort() noexcept;
  /// Closes a read handle or aborts a write handle, ignoring errors.
  void release() noexcept;

  [[nodiscard]] NcError fail(NcStatus status, NcOp op,
                             std::string_view object) const;
  [[nodiscard]] std::expected<int, NcError> id(NcOp op,
                                               std::string_view object) const;
  [[nodiscard]] std::expected<VarInfo, NcError> var(NcNameRef name,
                                                    NcOp op) const;
  [[nodiscard]] std::expected<VarInfo, NcError> var_info(int varid,
                                                         NcOp op) const;
  [[nodiscard]] std::expected<DimInfo, NcError> dim_info(int dimid,
                                                         NcOp op) const;
  /// The netCDF varid of `on` (NC_GLOBAL for a global attribute).
  [[nodiscard]] std::expected<int, NcError> att_owner(
      AttTarget on, NcOp op, std::string_view object) const;
  [[nodiscard]] std::expected<std::optional<AttShape>, NcError> att_shape(
      int varid, NcNameRef name, std::string_view object) const;
  /// The library's fill value for `varid` without a _FillValue attribute:
  /// nullopt if it is NC_NOFILL, or a byte variable (no default fill by
  /// convention: bytes are often flags).
  template <Numeric T>
  [[nodiscard]] std::expected<std::optional<T>, NcError> default_fill(
      int varid, NcNameRef var) const;

  // The type-independent halves of the templates: each template above is a
  // thin typed call on top of one of these.
  struct ReadPlan {
    int varid;
    std::size_t total;
  };
  /// The variable and element count of a read, after every check.
  [[nodiscard]] std::expected<ReadPlan, Error> plan_read(
      NcNameRef name, const Slab& slab, const ReadContext& ctx,
      bool (*readable)(Type) noexcept) const;
  struct AttPlan {
    int owner;
    std::size_t length;
  };
  /// Where a numeric attribute of `type` is and how long, or nullopt.
  [[nodiscard]] std::expected<std::optional<AttPlan>, NcError> plan_numeric_att(
      AttTarget on, NcNameRef name, Type type, std::size_t element_size) const;
  /// The owner of a new attribute of `bytes` bytes.
  [[nodiscard]] std::expected<int, NcError> plan_put_att(
      AttTarget on, NcNameRef name, std::size_t bytes) const;
  struct PutPlan {
    int varid;
    std::vector<std::size_t> start;
    std::vector<std::size_t> count;
  };
  /// The variable and hyperslab of a put of `size` values of `type`.
  [[nodiscard]] std::expected<PutPlan, NcError> plan_put(
      NcNameRef name, Type type, std::size_t size, const Slab& slab) const;
  /// Defines a numeric variable; `set_fill(ncid, varid)`, when given, sets
  /// its _FillValue.
  [[nodiscard]] std::expected<VarInfo, NcError> define_numeric_var(
      NcNameRef name, std::span<const DimInfo> dims, Type type,
      const std::optional<int>& deflate_level,
      const std::optional<std::vector<std::size_t>>& chunks,
      const std::function<int(int, int)>& set_fill);
  /// The varid of a variable whose masking of `type` is wanted, after the
  /// type and _Unsigned checks.
  [[nodiscard]] std::expected<int, NcError> plan_masking(NcNameRef name,
                                                         Type type) const;

  std::optional<int> ncid_;
  bool writable_{false};
  std::filesystem::path path_;
  ReadLimits limits_;
};

/// The body of write_netcdf_atomic: it defines and writes the file through
/// the File it is given and reports success or its own failure as
/// std::expected<void, E>, E convertible to io::Error. It must not close or
/// move the File.
template <class Body>
concept AtomicNcBody =
    std::invocable<Body, File&> and
    io::detail::is_expected_void_v<
        std::remove_cvref_t<std::invoke_result_t<Body, File&>>> and
    std::convertible_to<
        typename io::detail::ExpectedVoidError<
            std::remove_cvref_t<std::invoke_result_t<Body, File&>>>::type,
        Error>;

/// Replaces `target` with the netCDF-4 file `body` writes, atomically (C16):
/// a unique temporary file beside the target, created with NC_NOCLOBBER;
/// the body; close; fsync; rename over the target; fsync of the directory
/// (detail/atomic_file.hpp). On any failure the target is as it was and no
/// temporary file is left: a failed body is followed by nc_abort and the
/// body's error is returned unchanged; a close error is an NcError. An
/// existing target without owner write permission is `permission_denied`.
///
/// THREADING: not thread-safe; the caller serializes all calls into mov::io
/// netCDF functions.
template <AtomicNcBody Body>
[[nodiscard]] std::expected<void, Error> write_netcdf_atomic(
    const std::filesystem::path& target, Body&& body) {
  return detail::write_netcdf_atomic_impl(
      target, [&body](File& file) -> std::expected<void, Error> {
        return std::invoke(std::forward<Body>(body), file)
            .transform_error(lift<Error>);
      });
}

}  // namespace mov::io::nc
