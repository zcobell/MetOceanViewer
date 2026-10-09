// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/projection.hpp"

#include <proj.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <mutex>  // std::once_flag, std::call_once
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "mov/core/geo.hpp"
#include "mov/io/error.hpp"

namespace mov::io {

namespace {

// ---- where proj.db is ------------------------------------------------------

// Set once (set_projection_data_dir), then only read: the first call stores
// the directory and publishes a pointer to it; a reader loads the pointer.
// No lock in io (C11): call_once runs the store once, the atomic pointer
// makes it visible.
std::once_flag data_dir_once;
std::atomic<const std::string*> data_dir{nullptr};  // UTF-8

// The directory PROJ is told to search: $MOV_PROJ_DATA, else what the
// application set; nullopt leaves PROJ to its own defaults.
std::optional<std::string> configured_data_dir() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): read at context creation only
  if (const char* env = std::getenv("MOV_PROJ_DATA");
      env != nullptr and *env != '\0') {
    return std::string{env};
  }
  const std::string* dir = data_dir.load(std::memory_order_acquire);
  return dir == nullptr ? std::nullopt : std::optional{*dir};
}

std::string utf8(const std::filesystem::path& p) {
  const std::u8string text = p.u8string();
  std::string out;
  out.reserve(text.size());
  std::ranges::transform(text, std::back_inserter(out),
                         [](char8_t c) { return static_cast<char>(c); });
  return out;
}

// ---- PROJ objects ----------------------------------------------------------

struct ContextDeleter {
  void operator()(PJ_CONTEXT* ctx) const noexcept { proj_context_destroy(ctx); }
};
struct ObjectDeleter {
  void operator()(PJ* object) const noexcept { proj_destroy(object); }
};

using Context = std::unique_ptr<PJ_CONTEXT, ContextDeleter>;
using Object = std::unique_ptr<PJ, ObjectDeleter>;

// With a configured directory, PROJ must use exactly <dir>/proj.db: a
// package must use the database it ships. PROJ does not make that easy. A
// static PROJ (vcpkg's on Linux and macOS) carries a copy of proj.db built
// into the library, and falls back to it when it cannot open the file it is
// given, so has_database compares the path PROJ reports with the one required.
// The directory is also PROJ's search path for its other resource files.
struct DataSource {
  std::string dir;
  std::string database;
};

std::optional<DataSource> configured_source() {
  return configured_data_dir().transform([](std::string dir) {
    std::string database = dir + "/proj.db";
    return DataSource{.dir = std::move(dir), .database = std::move(database)};
  });
}

// A context that logs nothing (a failure is reported through the result)
// and opens the configured database, if any.
Context make_context(const std::optional<DataSource>& source) {
  Context ctx{proj_context_create()};
  if (not ctx) {
    return ctx;
  }
  proj_log_level(ctx.get(), PJ_LOG_NONE);
  if (source) {
    const char* const path = source->dir.c_str();
    proj_context_set_search_paths(ctx.get(), 1, &path);
    // On failure PROJ keeps another database or none; has_database tells.
    static_cast<void>(proj_context_set_database_path(
        ctx.get(), source->database.c_str(), nullptr, nullptr));
  }
  return ctx;
}

// PROJ opened a proj.db (the configured one, if any): the path exists, and
// the database answers a question about itself. (A file that is there but is
// not a database has a path and no answers.)
bool has_database(PJ_CONTEXT* ctx, const std::optional<DataSource>& source) {
  const char* const path = proj_context_get_database_path(ctx);
  if (path == nullptr or *path == '\0') {
    return false;
  }
  if (source and source->database != path) {
    return false;
  }
  return proj_context_get_database_metadata(
             ctx, "DATABASE.LAYOUT.VERSION.MAJOR") != nullptr;
}

// Only a CRS whose coordinates are positions on the ground can be turned into
// a Location: a vertical or geocentric CRS converts to meaningless angles.
bool is_geographic_crs(const PJ* crs) {
  const PJ_TYPE type = proj_get_type(crs);
  return type == PJ_TYPE_GEOGRAPHIC_2D_CRS or type == PJ_TYPE_GEOGRAPHIC_3D_CRS;
}

bool is_horizontal_crs(const PJ* crs) {
  switch (proj_get_type(crs)) {
    case PJ_TYPE_GEOGRAPHIC_2D_CRS:
    case PJ_TYPE_GEOGRAPHIC_3D_CRS:
    case PJ_TYPE_PROJECTED_CRS:
      return true;
    default:
      return false;
  }
}

std::string epsg_name(core::Epsg crs) {
  return std::format("EPSG:{}", crs.code());
}

// The accuracy, in metres, above which a transformation is called approximate.
constexpr double approximate_above_m = 1.0;

}  // namespace

// The PROJ objects, in the order they must be destroyed: the transformation
// first, the context last.
struct Projector::Impl {
  Context context;
  Object transformation;
};

Projector::Projector(core::Epsg crs, CrsKind kind,
                     std::unique_ptr<Impl> impl) noexcept
    : crs_{crs}, kind_{kind}, impl_{std::move(impl)} {}

Projector::Projector(Projector&&) noexcept = default;
Projector& Projector::operator=(Projector&&) noexcept = default;
Projector::~Projector() = default;

void set_projection_data_dir(const std::filesystem::path& dir) {
  std::call_once(data_dir_once, [&dir] {
    static const std::string stored = utf8(dir);
    data_dir.store(&stored, std::memory_order_release);
  });
}

std::optional<std::filesystem::path> projection_database_path() {
  const std::optional<DataSource> data = configured_source();
  const Context context = make_context(data);
  if (not context or not has_database(context.get(), data)) {
    return std::nullopt;
  }
  const std::string_view path = proj_context_get_database_path(context.get());
  return std::filesystem::path{std::u8string{path.begin(), path.end()}};
}

std::expected<Projector, ProjectionError> Projector::make(core::Epsg crs) {
  if (crs == core::Epsg::wgs84()) {
    return Projector{crs, CrsKind::geographic, nullptr};
  }
  const auto fail = [crs](ProjectionErrc code) {
    return std::unexpected{ProjectionError{.code = code, .crs = crs}};
  };
  const std::optional<DataSource> data = configured_source();
  Context context = make_context(data);
  if (not context) {
    return fail(ProjectionErrc::transform_failed);
  }
  if (not has_database(context.get(), data)) {
    return fail(ProjectionErrc::database_unavailable);
  }
  const std::string source = epsg_name(crs);
  const Object source_crs{proj_create(context.get(), source.c_str())};
  if (not source_crs or not is_horizontal_crs(source_crs.get())) {
    return fail(ProjectionErrc::unknown_crs);
  }
  const Object raw{proj_create_crs_to_crs(context.get(), source.c_str(),
                                          "EPSG:4326", nullptr)};
  if (not raw) {
    return fail(ProjectionErrc::transform_failed);
  }
  // Longitude/latitude order in and out, whatever the EPSG axis order says.
  Object normalized{proj_normalize_for_visualization(context.get(), raw.get())};
  if (not normalized) {
    return fail(ProjectionErrc::transform_failed);
  }
  return Projector{
      crs,
      is_geographic_crs(source_crs.get()) ? CrsKind::geographic
                                          : CrsKind::projected,
      std::make_unique<Impl>(Impl{.context = std::move(context),
                                  .transformation = std::move(normalized)})};
}

std::expected<CrsKind, ProjectionError> crs_kind(core::Epsg crs) {
  return Projector::make(crs).transform(
      [](const Projector& p) { return p.kind(); });
}

namespace {

// Asking PROJ which operation it used for a point makes it build a new
// operation object, about 200 microseconds against under one for the point
// itself (measured, 100000 points of EPSG:26915: 21 s against 0.07 s). So only
// a sample is asked: every one of the first points, which covers a small
// file entirely, then one in a few dozen.
constexpr std::size_t every_point_up_to = 256;
constexpr std::size_t then_every = 64;

bool is_sampled(std::size_t point_number) noexcept {
  return point_number <= every_point_up_to or point_number % then_every == 0;
}

// Adds what the operation PROJ used for the last point says about itself.
void note_last_operation(PJ_CONTEXT* ctx, PJ* transformation,
                         ProjectionAccuracy& accuracy) {
  ++accuracy.points;
  if (not is_sampled(accuracy.points)) {
    return;
  }
  ++accuracy.sampled_points;
  const Object used{proj_trans_get_last_used_operation(transformation)};
  if (not used) {
    return;
  }
  if (proj_coordoperation_has_ballpark_transformation(ctx, used.get()) != 0) {
    ++accuracy.ballpark_points;
  }
  const double stated = proj_coordoperation_get_accuracy(ctx, used.get());
  if (stated >= 0.0 and
      (not accuracy.worst_stated_m or stated > *accuracy.worst_stated_m)) {
    accuracy.worst_stated_m = stated;
  }
}

}  // namespace

std::expected<core::Xy, ProjectionError> Projector::project(core::Xy p) {
  if (not impl_) {
    return p;
  }
  const PJ_COORD result = proj_trans(impl_->transformation.get(), PJ_FWD,
                                     proj_coord(p.x, p.y, 0.0, HUGE_VAL));
  note_last_operation(impl_->context.get(), impl_->transformation.get(),
                      accuracy_);
  if (not std::isfinite(result.xy.x) or not std::isfinite(result.xy.y)) {
    return std::unexpected{
        ProjectionError{.code = ProjectionErrc::transform_failed, .crs = crs_}};
  }
  return core::Xy{.x = result.xy.x, .y = result.xy.y};
}

std::expected<core::Location, ToLocationError> Projector::to_location(
    core::Xy p) {
  return project(p)
      .transform_error(lift<ToLocationError>)
      .and_then([](core::Xy out) {
        return core::Location::make({.lat = out.y, .lon = out.x})
            .transform_error(lift<ToLocationError>);
      });
}

std::optional<Warning> Projector::approximation_warning() const {
  const bool approximate = accuracy_.ballpark_points > 0 or
                           (accuracy_.worst_stated_m and
                            *accuracy_.worst_stated_m > approximate_above_m);
  if (not approximate) {
    return std::nullopt;
  }
  return Warning{.code = WarningCode::crs_approximate,
                 .subject = epsg_name(crs_),
                 .count = accuracy_.points};
}

std::expected<core::Location, ToLocationError> to_location(
    const core::NativePoint& p) {
  if (p.crs() == core::Epsg::wgs84()) {
    return core::Location::make({.lat = p.y(), .lon = p.x()})
        .transform_error(lift<ToLocationError>);
  }
  return Projector::make(p.crs())
      .transform_error(lift<ToLocationError>)
      .and_then([&p](Projector projector) {
        return projector.to_location({.x = p.x(), .y = p.y()});
      });
}

}  // namespace mov::io
