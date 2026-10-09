// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The netCDF-C wrapper (core-design.md section 4). Every netCDF-C call of
// mov::io is made through it; nothing outside src/io/netcdf/ includes
// <netcdf.h>.
//
// THREADING: netCDF-C (and the HDF5 under it) is not thread-safe, and this
// wrapper adds no lock. The caller must serialize all calls into mov::io
// netCDF functions (every member of File and NewFile, write_netcdf_atomic and
// every reader built on them), across all files: the providers and the app
// run them on one serial queue (C11). `const` on a File member means it does
// not change the File; it does NOT mean the member may be called
// concurrently, even on different files. Debug builds detect concurrent or
// re-entrant entry and abort.
//
// Blocking I/O: call these from a worker, never the GUI thread.

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
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

class NewFile;

namespace detail {

/// What write_netcdf_atomic runs on the new file.
using NcBodyFunction = std::function<std::expected<void, Error>(NewFile&)>;

/// A numeric attribute fetched in the widest exact form of its kind:
/// integers as int64, floating-point values as double.
using AttNumbers = std::variant<std::vector<std::int64_t>, std::vector<double>>;

/// write_netcdf_atomic with its stages exposed to a FaultInjector (tests).
[[nodiscard]] std::expected<void, Error> write_netcdf_atomic_impl(
    const std::filesystem::path& target, const ReadLimits& limits,
    const NcBodyFunction& body, const io::detail::FaultInjector& fault = {});

/// The netCDF id, the path errors name and the limits, with the inquiries
/// File and NewFile share. Not part of the API.
class Dataset {
 protected:
  Dataset(int ncid, std::filesystem::path path,
          const ReadLimits& limits) noexcept;
  Dataset(Dataset&& other) noexcept;
  Dataset& operator=(Dataset&& other) noexcept;
  Dataset(const Dataset&) = delete;
  Dataset& operator=(const Dataset&) = delete;
  ~Dataset() = default;

  struct AttShape {
    Type type;
    std::size_t length;
  };

  [[nodiscard]] NcError fail(NcStatus status, NcOp op,
                             std::string_view object) const;
  /// An error about attribute `name` of `on`, named `var:att`.
  [[nodiscard]] NcError att_fail(NcStatus status, NcOp op, AttTarget on,
                                 NcNameRef name) const;
  /// A netCDF-C status about attribute `name` of `on`; the name is formatted
  /// only for an error.
  [[nodiscard]] std::expected<void, NcError> att_status(int status, NcOp op,
                                                        AttTarget on,
                                                        NcNameRef name) const;
  [[nodiscard]] std::expected<int, NcError> id(NcOp op,
                                               std::string_view object) const;
  [[nodiscard]] std::expected<VarInfo, NcError> var(NcNameRef name,
                                                    NcOp op) const;
  [[nodiscard]] std::expected<VarInfo, NcError> var_info(int varid,
                                                         NcOp op) const;
  [[nodiscard]] std::expected<DimInfo, NcError> dim_info(int dimid,
                                                         NcOp op) const;
  /// The netCDF varid of `on` (NC_GLOBAL for a global attribute).
  [[nodiscard]] std::expected<int, NcError> att_owner(AttTarget on,
                                                      NcNameRef name,
                                                      NcOp op) const;
  [[nodiscard]] std::expected<std::optional<AttShape>, NcError> att_shape(
      int varid, AttTarget on, NcNameRef name) const;
  /// Gives up the id without calling netCDF-C: after a failed close the id
  /// must not be touched again (see File).
  void forget() noexcept { ncid_.reset(); }

  std::optional<int> ncid_;
  std::filesystem::path path_;
  ReadLimits limits_;
};

}  // namespace detail

/// An open netCDF file, read-only. Move-only; a moved-from or closed File
/// answers every call with WrapperFault::closed (and keeps its path, for the
/// error).
///
/// Errors: a netCDF-C status is LibraryStatus (positive codes are errno
/// values, as in netCDF-C); what the wrapper refuses itself is a
/// WrapperFault. NcError::object names the variable or dimension, or an
/// attribute as `var:att` (`:att` for a global one), in ncdump's notation;
/// NcError::file is the path given to open.
///
/// Close policy: the destructor closes and ignores a close error; close()
/// reports it. A failed nc_close is never followed by nc_abort: netCDF-C
/// 4.9.3 frees part of the file's state before it reports a close error
/// (libhdf5/hdf5internal.c, libsrc/nc3internal.c), so aborting would free it
/// twice. The id is given up instead, and the library keeps that one entry
/// (a bounded leak, only after a failed close).
class File : private detail::Dataset {
 public:
  /// Opens `path` read-only. Only a regular file is opened (a directory is
  /// EISDIR, anything else EINVAL, both as LibraryStatus) so a FIFO cannot
  /// block. `limits` bounds every read of this File (the one source of
  /// truth: the reads take only a StopToken).
  ///
  /// ONE HANDLE PER FILE: do not open a file that another File of this
  /// process still has open (not even through a symbolic or hard link).
  /// netCDF-C 4.9.3 with HDF5 2.1.1 can segfault in HDF5 (the vlen fill value
  /// of an NC_STRING variable is converted with a null file pointer) when a
  /// file is open through two handles, one is closed after reading strings and
  /// the file is then opened again. A reader opens its file once and closes it
  /// before it returns; code that needs two views of a file passes the one
  /// handle around. Debug builds keep a list of the open files and assert on
  /// a second open of the same file (like the entry check of nc_call, a
  /// detector, not a lock); release builds check nothing.
  [[nodiscard]] static std::expected<File, NcError> open(
      const std::filesystem::path& path, const ReadLimits& limits);

  File(File&& other) noexcept = default;
  File& operator=(File&& other) noexcept;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File();

  /// Closes the file and reports a close error. The handle is given up
  /// either way.
  [[nodiscard]] std::expected<void, NcError> close() &&;

  [[nodiscard]] bool is_open() const noexcept { return ncid_.has_value(); }
  /// The path errors name.
  [[nodiscard]] const std::filesystem::path& path() const& noexcept {
    return path_;
  }
  const std::filesystem::path& path() const&& = delete;
  [[nodiscard]] const ReadLimits& limits() const& noexcept { return limits_; }
  const ReadLimits& limits() const&& = delete;

  // ---- structure ----------------------------------------------------------
  // An absent dimension or variable is nullopt, never id 0 (B12).

  [[nodiscard]] std::expected<std::optional<DimInfo>, NcError> find_dim(
      NcNameRef name) const;
  [[nodiscard]] std::expected<std::optional<VarInfo>, NcError> find_var(
      NcNameRef name) const;
  /// Every variable of the root group, in id order.
  [[nodiscard]] std::expected<std::vector<VarInfo>, NcError> variables() const;
  /// The chunk sizes of variable `name`, one per dimension (as stored: a
  /// chunk is read and decompressed whole, so a reader that wants few chunks
  /// wants to know them), or nullopt when the variable is not chunked:
  /// contiguous or compact storage, which is all a classic file has.
  [[nodiscard]] std::expected<std::optional<std::vector<std::size_t>>, NcError>
  chunk_shape(NcNameRef name) const;
  /// Makes the chunk cache of variable `name` at least `bytes` big (it never
  /// shrinks one). netCDF-C keeps 16 MiB per variable by default, and a chunk
  /// bigger than the cache is decompressed again by each read that touches it:
  /// a deflated chunk of 80 MB read in blocks of 8 MB is decompressed ten
  /// times. The cache is memory the read holds until the File is closed, so a
  /// caller bounds `bytes` (a reader by ReadLimits::max_result_bytes). `const`
  /// because it changes the library's cache for this file, not the File's
  /// state.
  [[nodiscard]] std::expected<void, NcError> reserve_chunk_cache(
      NcNameRef name, std::size_t bytes) const;

  // ---- attributes ----------------------------------------------------------
  // An absent attribute is nullopt; an absent variable is an error.

  /// The external type of the attribute, nullopt when there is none: one
  /// inquiry, for a reader that accepts several types of a number.
  [[nodiscard]] std::expected<std::optional<Type>, NcError> att_type(
      AttTarget on, NcNameRef name) const;

  /// An NC_CHAR attribute (all attlen bytes, NULs included, B8) or a
  /// one-element NC_STRING attribute (a NULL string is ""). Longer than
  /// max_att_bytes is `too_large`; any other type is `type_mismatch`, an
  /// NC_STRING attribute of several strings `count_mismatch`.
  [[nodiscard]] std::expected<std::optional<std::string>, NcError> text_att(
      AttTarget on, NcNameRef name) const;

  /// A numeric attribute whose type is exactly T's (`type_mismatch`
  /// otherwise: an EPSG code stored as text is not a number, B10).
  template <Numeric T>
  [[nodiscard]] std::expected<std::optional<std::vector<T>>, NcError>
  numeric_att(AttTarget on, NcNameRef name) const;

  // ---- data ----------------------------------------------------------------
  // Before anything is allocated, the slab must lie inside the dimensions
  // (NC_EINVALCOORDS, NC_EEDGE) and its element count must fit in size_t
  // (`overflow`). A read that returns the whole slab (read, read_samples)
  // also keeps it within limits.max_elements and its bytes within
  // limits.max_result_bytes (`too_large`; peaks in read_limits.hpp);
  // read_blocks holds one block, so each block is held to those limits
  // instead, and a slab of any size can be walked.
  // Reads go in blocks of rows_per_block(slab, limits.slab_elements) outer
  // indices, polling `stop` before each (Cancelled). Errors are therefore
  // io::Error.

  /// The values of `slab` of variable `name`, converted to T only where no
  /// value can change (readable_as, section 4.3): a float variable reads as
  /// float or double, never a double variable as float (B4). An int64
  /// variable reads as double for times only: a double is exact only below
  /// 2^53, which checked_time enforces; read any other int64 variable as
  /// int64_t.
  template <Numeric T>
  [[nodiscard]] std::expected<std::vector<T>, Error> read(
      NcNameRef name, const Slab& slab, const StopToken& stop = {}) const;

  /// What read_blocks hands its visitor: the values of one block, and the
  /// outer indices (of the first dimension; {0, 1} for a scalar) it covers.
  /// Every inner dimension is the whole of its slab range. The span is valid
  /// only during the call. Returning an error stops the read with it.
  template <Numeric T>
  using BlockVisitor = std::function<std::expected<void, Error>(
      std::span<const T> values, DimRange outer)>;

  /// read, one block at a time through one reused buffer: for a caller that
  /// consumes a large slab piecewise (a time block of every station, WP9).
  /// The checks and limits are read's.
  template <Numeric T>
  [[nodiscard]] std::expected<void, Error> read_blocks(
      NcNameRef name, const Slab& slab, const BlockVisitor<T>& visit,
      const StopToken& stop = {}) const;

  /// The missing-data attributes of variable `name`, whose type must be
  /// exactly T's. _FillValue must have T's type and one value. missing_value,
  /// valid_min, valid_max and valid_range may have any numeric type whose
  /// every value converts to T exactly (a netCDF4-python double
  /// missing_value on a float variable); scale_factor and add_offset any
  /// numeric type that converts to double exactly. Otherwise
  /// `type_mismatch`; a wrong number of values is `count_mismatch`.
  /// `_Unsigned = "true"` is `unsupported_unsigned`.
  template <Numeric T>
  [[nodiscard]] std::expected<Masking<T>, NcError> masking(
      NcNameRef name) const;

  /// read, masked and unpacked in the variable's own type before widening
  /// (B4, B9), block by block straight into the result: byte, short, int,
  /// float and double variables. 64-bit integer and every other type are
  /// `type_mismatch`.
  [[nodiscard]] std::expected<std::vector<core::Sample>, Error> read_samples(
      NcNameRef name, const Slab& slab, const StopToken& stop = {}) const;

  /// The rows of an NC_CHAR variable of rank >= 1: the last dimension is the
  /// row length (the stride comes from the file, B7, B11), every other index
  /// one row, in row-major order. Raw: NULs and the bytes after them are
  /// kept; the caller applies its policy (C14). Another type is
  /// `type_mismatch`, a scalar `rank_mismatch`.
  [[nodiscard]] std::expected<std::vector<std::string>, Error> read_char_rows(
      NcNameRef name, const StopToken& stop = {}) const;

  /// Every element of an NC_STRING variable, in row-major order; a NULL
  /// element is "". The library's strings are always freed (B21).
  [[nodiscard]] std::expected<std::vector<std::string>, Error> read_strings(
      NcNameRef name, const StopToken& stop = {}) const;

 private:
  File(int ncid, std::filesystem::path path, const ReadLimits& limits) noexcept
      : Dataset{ncid, std::move(path), limits} {}
  /// Closes, ignoring the status, and gives up the id (destructor, move
  /// assignment).
  void drop() noexcept;

  // The type-independent halves of the templates: each template is a thin
  // typed call on top of one of these.
  struct ReadPlan {
    int varid;
    std::size_t total;
  };
  /// The variable and element count of a read, after every check; the
  /// result has `total` elements of `element_bytes`. `whole_result` is false
  /// for read_blocks, whose blocks are checked as they are read.
  [[nodiscard]] std::expected<ReadPlan, Error> plan_read(
      NcNameRef name, const Slab& slab, bool (*readable)(Type) noexcept,
      std::size_t element_bytes, bool whole_result = true) const;
  struct Block {
    std::span<const std::size_t> start;
    std::span<const std::size_t> count;
    DimRange outer;
    std::size_t offset;  // of its first element in the whole read
    std::size_t size;
  };
  using BlockReader = std::function<std::expected<void, Error>(const Block&)>;
  /// Calls `read` for each block of `slab` in order, polling `stop`.
  [[nodiscard]] std::expected<void, Error> for_each_block(
      const Slab& slab, const StopToken& stop, const BlockReader& read) const;
  /// read_blocks for one variable already planned.
  template <Numeric T>
  [[nodiscard]] std::expected<void, Error> stream(
      NcNameRef name, int varid, const Slab& slab, const StopToken& stop,
      const BlockVisitor<T>& visit) const;

  struct AttPlan {
    int owner;
    std::size_t length;
  };
  /// Where a numeric attribute of `type` is and how long, or nullopt.
  [[nodiscard]] std::expected<std::optional<AttPlan>, NcError> plan_numeric_att(
      AttTarget on, NcNameRef name, Type type, std::size_t element_size) const;

  using AttNumbers = detail::AttNumbers;
  struct MaskingPlan {
    int varid;
    bool has_fill;
    std::optional<AttNumbers> missing{};
    std::optional<AttNumbers> range{};  // valid_range, two values
    std::optional<AttNumbers> min{};
    std::optional<AttNumbers> max{};
    std::optional<double> scale{};
    std::optional<double> offset{};
  };
  [[nodiscard]] std::expected<MaskingPlan, NcError> plan_masking(
      NcNameRef name, Type type) const;
  [[nodiscard]] std::expected<std::optional<AttNumbers>, NcError> att_numbers(
      int varid, NcNameRef var, NcNameRef att, std::size_t count) const;
  [[nodiscard]] std::expected<std::optional<double>, NcError> att_double(
      int varid, NcNameRef var, NcNameRef att) const;
  /// The library's fill value for `varid` without a _FillValue attribute:
  /// nullopt if it is NC_NOFILL, or a byte variable (no default fill by
  /// convention: bytes are often flags).
  template <Numeric T>
  [[nodiscard]] std::expected<std::optional<T>, NcError> default_fill(
      int varid, NcNameRef var) const;
};

/// The new file inside write_netcdf_atomic: the only way to write netCDF.
/// Its body gets it by reference; it can be neither copied nor moved, so it
/// cannot outlive the write. The atomic writer closes it; destroyed open (the
/// body failed or threw) it ends define mode and aborts, which netCDF-C does
/// without deleting anything itself (TempFileGuard removes the temporary
/// file).
class NewFile : private detail::Dataset {
 public:
  NewFile(const NewFile&) = delete;
  NewFile& operator=(const NewFile&) = delete;
  NewFile(NewFile&&) = delete;
  NewFile& operator=(NewFile&&) = delete;
  ~NewFile();

  /// The target of the write (errors name it, not the temporary file).
  [[nodiscard]] const std::filesystem::path& path() const& noexcept {
    return path_;
  }
  const std::filesystem::path& path() const&& = delete;

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
  /// put_att of a contiguous container (std::vector, std::array): T is its
  /// element type.
  template <std::ranges::contiguous_range R>
    requires Numeric<std::ranges::range_value_t<R>>
  [[nodiscard]] std::expected<void, NcError> put_att(AttTarget on,
                                                     NcNameRef name,
                                                     const R& values) {
    return put_att<std::ranges::range_value_t<R>>(on, name, std::span{values});
  }

  /// Writes `data` to `slab` of variable `name`, whose type must be exactly
  /// T's (`type_mismatch`); data.size() must equal the slab's element count
  /// (`count_mismatch`).
  template <Numeric T>
  [[nodiscard]] std::expected<void, NcError> put(NcNameRef name,
                                                 std::span<const T> data,
                                                 const Slab& slab);
  template <std::ranges::contiguous_range R>
    requires Numeric<std::ranges::range_value_t<R>>
  [[nodiscard]] std::expected<void, NcError> put(NcNameRef name, const R& data,
                                                 const Slab& slab) {
    return put<std::ranges::range_value_t<R>>(name, std::span{data}, slab);
  }

  /// Writes one row per element of the first dimension of a 2-D NC_CHAR
  /// variable, NUL-padded to the second (B15). The rows are anything that
  /// converts to std::string_view. Their number must equal the first
  /// dimension (`count_mismatch`); a longer row is `name_too_long`.
  template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>,
                                 std::string_view>
  [[nodiscard]] std::expected<void, NcError> put_char_rows(NcNameRef name,
                                                           const R& rows) {
    std::vector<std::string_view> views;
    for (auto&& row : rows) {
      views.emplace_back(row);
    }
    return put_char_rows_impl(name, views);
  }

  [[nodiscard]] std::expected<void, NcError> end_define();

 private:
  friend std::expected<void, Error> detail::write_netcdf_atomic_impl(
      const std::filesystem::path& target, const ReadLimits& limits,
      const detail::NcBodyFunction& body,
      const io::detail::FaultInjector& fault);

  NewFile(int ncid, std::filesystem::path target,
          const ReadLimits& limits) noexcept
      : Dataset{ncid, std::move(target), limits} {}

  /// NC_NETCDF4 | NC_NOCLOBBER at `temp`: the new netCDF id. Errors name
  /// `target`.
  [[nodiscard]] static std::expected<int, NcError> create(
      const std::filesystem::path& temp, const std::filesystem::path& target);
  /// The atomic writer's stage 3: nc_sync (which ends define mode), then
  /// nc_close. A failed sync has released nothing, so the file is aborted; a
  /// failed close is never followed by an abort (see File); the id is given
  /// up.
  [[nodiscard]] std::expected<void, NcError> finish();
  /// Ends define mode (status ignored), then nc_abort: with define mode
  /// over, netCDF-C's abort deletes no file (its own copy of a long path is
  /// truncated, netCDF-C 4.9.3 libhdf5/hdf5file.c NC4_abort).
  void abandon() noexcept;

  [[nodiscard]] std::expected<int, NcError> plan_put_att(
      AttTarget on, NcNameRef name, std::size_t bytes) const;
  struct PutPlan {
    int varid;
    detail::Hyperslab slab;
  };
  [[nodiscard]] std::expected<PutPlan, NcError> plan_put(
      NcNameRef name, Type type, std::size_t size, const Slab& slab) const;
  /// Defines a numeric variable; `set_fill(ncid, varid)`, when given, sets
  /// its _FillValue.
  [[nodiscard]] std::expected<VarInfo, NcError> define_numeric_var(
      NcNameRef name, std::span<const DimInfo> dims, Type type,
      const std::optional<int>& deflate_level,
      const std::optional<std::vector<std::size_t>>& chunks,
      const std::function<int(int, int)>& set_fill);
  [[nodiscard]] std::expected<void, NcError> put_char_rows_impl(
      NcNameRef name, std::span<const std::string_view> rows);
};

/// The body of write_netcdf_atomic: it defines and writes the file through
/// the NewFile it is given and reports success or its own failure as
/// std::expected<void, E>, E convertible to io::Error.
template <class Body>
concept AtomicNcBody =
    std::invocable<Body, NewFile&> and
    io::detail::is_expected_void_v<
        std::remove_cvref_t<std::invoke_result_t<Body, NewFile&>>> and
    std::convertible_to<
        typename io::detail::ExpectedVoidError<
            std::remove_cvref_t<std::invoke_result_t<Body, NewFile&>>>::type,
        Error>;

/// Replaces `target` with the netCDF-4 file `body` writes, atomically (C16):
/// a unique temporary file beside the target, created with NC_NOCLOBBER;
/// the body; sync and close; fsync; rename over the target; fsync of the
/// directory (detail/atomic_file.hpp). On any failure the target is as it
/// was and no temporary file is left: a failed body is followed by an abort
/// and the body's error is returned unchanged; a sync or close error is an
/// NcError. An existing target without owner write permission is
/// `permission_denied`. `limits` bounds the attributes the body writes.
///
/// THREADING: not thread-safe; the caller serializes all calls into mov::io
/// netCDF functions.
template <AtomicNcBody Body>
[[nodiscard]] std::expected<void, Error> write_netcdf_atomic(
    const std::filesystem::path& target, const ReadLimits& limits,
    Body&& body) {
  return detail::write_netcdf_atomic_impl(
      target, limits, [&body](NewFile& file) -> std::expected<void, Error> {
        return std::invoke(std::forward<Body>(body), file)
            .transform_error(lift<Error>);
      });
}

}  // namespace mov::io::nc
