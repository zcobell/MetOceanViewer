// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include "mov/core/cancelled.hpp"

namespace mov::io {

// Errors are narrow inside core: each function returns its own small type.
// From io upward there is one variant per layer, and `lift<V>` moves a narrow
// error into it. Nothing here formats text; `describe()` is for the edge.

// ---- files ---------------------------------------------------------------

enum class FileOp : std::uint8_t {
  open,
  create,  // the temporary file of an atomic write
  size,
  read,
  write,
  close,
  fsync,      // the temporary file
  fsync_dir,  // the directory that holds the target, after the rename
  rename,
  permissions,  // copying the mode of the replaced file
  remove,
};

/// A filesystem failure. A file over the size limit is `size` with
/// `std::errc::file_too_large`; a path that is not a regular file is `open`
/// with `is_a_directory` (a directory) or `invalid_argument` (anything else).
struct FileError {
  FileOp op;
  std::filesystem::path path;
  std::error_code ec;
  friend bool operator==(const FileError&, const FileError&) = default;
};

// ---- text ----------------------------------------------------------------

enum class ParseErrc : std::uint8_t {
  empty_input,
  missing_header,
  wrong_field_count,
  bad_integer,
  bad_number,
  bad_date,
  bad_time_units,
  time_out_of_range,
  out_of_range,
  count_mismatch,
  corrupt_record,
  trailing_text,
  /// A ReadContext limit was exceeded: `context` says how much the input holds
  /// and the limit; `line` is where the reader noticed (1-based, or the last
  /// line read). A file that is too big for `max_text_bytes` is a FileError
  /// (`file_too_large`) at open instead.
  too_large,
};

namespace detail {

/// The longest prefix of `text` that has at most `max_bytes` bytes and does
/// not end inside a UTF-8 sequence: it backs up over at most three
/// continuation bytes, the longest tail of a valid sequence. Text that is not
/// UTF-8 is cut at `max_bytes`.
[[nodiscard]] std::string_view truncate_utf8(std::string_view text,
                                             std::size_t max_bytes) noexcept;

}  // namespace detail

/// A syntax or range problem in a text input. `line` is 1-based, `column` a
/// 0-based byte offset in that line when one applies. `context` is the
/// offending text, at most `max_context_bytes` long and cut on a UTF-8
/// boundary, so a hostile megabyte line cannot end up in an error value.
///
/// `column` refers to the text as given to the parser, not to any trimmed or
/// folded copy. `context` is raw input: bytes that are not UTF-8, control
/// characters and markup included. Whatever shows it (a log, the UI) must
/// escape it.
class ParseError {
 public:
  static constexpr std::size_t max_context_bytes = 120;

  /// Where the problem is; the column is left out for whole-line errors.
  struct Where {
    std::size_t line;
    std::optional<std::size_t> column{};
  };

  [[nodiscard]] static ParseError make(ParseErrc code, Where where,
                                       std::string_view context);

  [[nodiscard]] ParseErrc code() const noexcept { return code_; }
  [[nodiscard]] std::size_t line() const noexcept { return line_; }
  [[nodiscard]] std::optional<std::size_t> column() const noexcept {
    return column_;
  }
  [[nodiscard]] const std::string& context() const& noexcept {
    return context_;
  }
  const std::string& context() && = delete;

  friend bool operator==(const ParseError&, const ParseError&) = default;

 private:
  ParseError(ParseErrc code, Where where, std::string context)
      : code_{code},
        line_{where.line},
        column_{where.column},
        context_{std::move(context)} {}

  ParseErrc code_;
  std::size_t line_;
  std::optional<std::size_t> column_;
  std::string context_;
};

// ---- netCDF (the wrapper: mov/io/netcdf/file.hpp) ------------------------

enum class NcOp : std::uint8_t {
  open,
  create,
  close,
  abort,
  sync,  // the flush (and end of define mode) before a write handle closes
  inquire,
  get_var,
  put_var,
  get_att,
  put_att,
  def_dim,
  def_var,
  enddef,
  free_string,
};

/// Misuse the wrapper itself detects, as opposed to a netCDF-C status.
enum class WrapperFault : std::uint8_t {
  type_mismatch,
  rank_mismatch,
  count_mismatch,
  overflow,
  too_large,
  zero_length_dim,
  name_too_long,
  unsupported_unsigned,
  closed,
  unrepresentable_path,  // Windows: not expressible in the code page
                         // netCDF-C reads paths in (netcdf/path.cpp)
};

/// A nonzero netCDF-C status code.
struct LibraryStatus {
  int code;
  friend constexpr bool operator==(LibraryStatus, LibraryStatus) = default;
};

using NcStatus = std::variant<LibraryStatus, WrapperFault>;

struct NcError {
  NcStatus status;
  NcOp op;
  std::string object;  // variable, dimension or attribute name; may be empty
  std::filesystem::path file;
  friend bool operator==(const NcError&, const NcError&) = default;
};

// ---- structure -----------------------------------------------------------

enum class FormatErrc : std::uint8_t {
  not_this_format,
  missing_variable,
  missing_dimension,
  missing_attribute,
  partner_variable_missing,
  station_count_mismatch,
  cold_start_required,
  time_missing,
  time_out_of_range,
  time_not_increasing,
  layer_out_of_range,
  wrong_column_count,
  noncanonical_unit,
  duplicate_quantity,
  // station netCDF, SN section 12
  unsupported_version,
  bad_version,
  no_station_id,
  duplicate_station_id,
  bad_coordinates,
  bad_obs_count,
  padding_not_missing,
  bad_encoding,
  wet_dry_inconsistent,
  bad_flag,
  unsupported_layout,
  unsupported_calendar,
  unsupported_crs,
  projection_unavailable,  // the CRS is fine, the projection database is not
  no_data_variables,
  bad_ancillary,
  dimension_mismatch,
  // writers
  empty_collection,
  invalid_variable_name,  // a token the format cannot use as a variable name
  no_samples,             // no station has a sample
  too_many_samples,       // more samples than the format can count
  bad_option,             // a writer option the format cannot store
  // foreign and legacy station netCDF
  bad_row_size,      // a contiguous ragged count that is negative, missing or
                     // does not add up to the sample dimension
  bad_ragged_index,  // an indexed ragged instance index outside the stations
  inconsistent_metadata,  // stations of a legacy file that disagree on the
                          // units or datum of their one shared column
  ambiguous_station_id,   // several variables claim to be the station id
};

/// A file that parses but does not mean what its format requires.
struct FormatError {
  FormatErrc code;
  std::string subject;  // the variable, attribute or id at fault
  std::optional<std::size_t> station{};
  std::optional<std::size_t> index{};
  friend bool operator==(const FormatError&, const FormatError&) = default;
};

/// The caller asked to stop (`ReadContext::stop`): the core's Cancelled, so
/// io and the core's long computations share one alternative.
using core::Cancelled;

/// The one error type of the io layer.
using Error =
    std::variant<FileError, ParseError, NcError, FormatError, Cancelled>;

/// Moves a narrow error into a layer's error variant `V`:
/// `result.transform_error(lift<io::Error>)`. It compiles only when exactly
/// the conversion is valid, and an `E` that already is a `V` passes through.
template <class V>
inline constexpr auto lift = []<class E>(E&& e) -> V
  requires std::constructible_from<V, E>
{ return V{std::forward<E>(e)}; };

}  // namespace mov::io
