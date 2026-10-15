// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/detail/numeric.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"

// What a harmonics file says about one station (docs/harmonics-json.md
// section 8, docs/harmonics-engine.md 2.5): its metadata, a reference
// station's harmonic constants, a subordinate station's offsets.

namespace mov::core::tide {

/// How the source derived a reference station's constants
/// (docs/harmonics-json.md 8.2); absent means unknown. Informative only.
enum class Analysis : std::uint8_t {
  nos,
  utide,
  ttide,
  least_squares,
  published
};

inline constexpr std::array<Analysis, 5> all_analyses{
    Analysis::nos, Analysis::utide, Analysis::ttide, Analysis::least_squares,
    Analysis::published};
static_assert(static_cast<std::size_t>(Analysis::published) + 1 ==
              all_analyses.size());

/// The file token. A switch without default, each enumerator beside its
/// token; the schema's `analysis` enum is compared with it by a test.
[[nodiscard]] constexpr std::string_view to_token(Analysis a) noexcept {
  switch (a) {
    case Analysis::nos:
      return "nos";
    case Analysis::utide:
      return "utide";
    case Analysis::ttide:
      return "ttide";
    case Analysis::least_squares:
      return "least_squares";
    case Analysis::published:
      return "published";
  }
  return "published";  // unreachable: every enumerator is handled
}

[[nodiscard]] constexpr std::optional<Analysis> parse_analysis(
    std::string_view token) noexcept {
  for (const Analysis a : all_analyses) {
    if (to_token(a) == token) {
      return a;
    }
  }
  return std::nullopt;
}

/// The standard deviations of a constant: amplitude >= 0, phase in
/// [0, 360] degrees (a spread, not an angle, so 360 is not 0).
struct Uncertainty {
  Length amplitude;
  double phase_deg;
  friend constexpr bool operator==(const Uncertainty&,
                                   const Uncertainty&) = default;
};

/// Which value of a row is out of range.
enum class HarmonicConstantError : std::uint8_t {
  amplitude,     // not finite or < 0
  phase,         // not in [0, 360)
  amplitude_sd,  // not finite or < 0
  phase_sd       // not in [0, 360]
};

/// One station row's amplitude H, Greenwich phase lag G and optional
/// standard deviations (docs/harmonics-json.md 8.2), checked: H finite and
/// >= 0, G in [0, 360). The schema's upper bounds (H <= 100 m) are the
/// reader's to check; they are not invariants.
class HarmonicConstant {
 public:
  struct UncertaintyValues {
    double amplitude_m;
    double phase_deg;
  };
  struct Values {
    double amplitude_m;
    double phase_deg;
    std::optional<UncertaintyValues> sd;
  };

  /// The first failing value, in row order, is the error.
  [[nodiscard]] static constexpr std::expected<HarmonicConstant,
                                               HarmonicConstantError>
  make(const Values& v) noexcept {
    if (not non_negative(v.amplitude_m)) {
      return std::unexpected(HarmonicConstantError::amplitude);
    }
    if (not(v.phase_deg >= 0.0 and v.phase_deg < 360.0)) {
      return std::unexpected(HarmonicConstantError::phase);
    }
    std::optional<Uncertainty> sd;
    if (v.sd) {
      if (not non_negative(v.sd->amplitude_m)) {
        return std::unexpected(HarmonicConstantError::amplitude_sd);
      }
      if (not(v.sd->phase_deg >= 0.0 and v.sd->phase_deg <= 360.0)) {
        return std::unexpected(HarmonicConstantError::phase_sd);
      }
      sd = Uncertainty{
          .amplitude = Length::in(v.sd->amplitude_m, LengthUnit::meter),
          .phase_deg = v.sd->phase_deg};
    }
    // [0, 360) is already reduced, so wrap() keeps the value exactly.
    return HarmonicConstant{Length::in(v.amplitude_m, LengthUnit::meter),
                            Phase::wrap(v.phase_deg), sd};
  }

  [[nodiscard]] constexpr Length amplitude() const noexcept {
    return amplitude_;
  }
  [[nodiscard]] constexpr Phase phase() const noexcept { return phase_; }
  [[nodiscard]] constexpr const std::optional<Uncertainty>& sd()
      const& noexcept {
    return sd_;
  }
  const std::optional<Uncertainty>& sd() const&& = delete;  // would dangle

  friend constexpr bool operator==(const HarmonicConstant&,
                                   const HarmonicConstant&) = default;

 private:
  constexpr HarmonicConstant(Length amplitude, Phase phase,
                             std::optional<Uncertainty> sd) noexcept
      : amplitude_{amplitude}, phase_{phase}, sd_{sd} {}

  [[nodiscard]] static constexpr bool non_negative(double x) noexcept {
    return detail::is_finite(x) and x >= 0.0;
  }

  Length amplitude_;
  Phase phase_;
  std::optional<Uncertainty> sd_;
};

/// A reference station's constant of one constituent of the file's set.
struct Constant {
  ConstituentIndex constituent;
  HarmonicConstant value;
  friend constexpr bool operator==(const Constant&, const Constant&) = default;
};

/// Subordinate heights as ratios of the reference's (> 0, finite).
struct HeightRatio {
  HighLow<double> ratio;
  friend constexpr bool operator==(const HeightRatio&,
                                   const HeightRatio&) = default;
};
/// Subordinate heights as the reference's plus an offset (finite).
struct HeightOffset {
  HighLow<Length> offset;
  friend constexpr bool operator==(const HeightOffset&,
                                   const HeightOffset&) = default;
};
using HeightAdjustment = std::variant<HeightRatio, HeightOffset>;

/// Which value of a subordinate's offsets is out of range.
enum class SubordinateOffsetsError : std::uint8_t {
  high_time,    // time_min.high not finite or |minutes| > 1440
  low_time,     // time_min.low
  high_height,  // height.high: a ratio not finite or <= 0, an offset not
                // finite
  low_height    // height.low
};

/// A subordinate station's offsets from its reference (docs/harmonics-json.md
/// 8.3): time offsets in whole milliseconds, a height ratio or additive
/// offset, and the datum D the heights are relative to.
class SubordinateOffsets {
 public:
  static constexpr double max_abs_minutes = 1440.0;

  struct Values {
    HighLow<double> minutes;
    HeightAdjustment height;
    TideDatum datum;
  };

  /// Minutes become milliseconds rounded half away from zero (-0 is 0).
  /// The first failing value, in the order of SubordinateOffsetsError, is
  /// the error.
  [[nodiscard]] static constexpr std::expected<SubordinateOffsets,
                                               SubordinateOffsetsError>
  make(const Values& v) noexcept {
    if (not time_ok(v.minutes.high)) {
      return std::unexpected(SubordinateOffsetsError::high_time);
    }
    if (not time_ok(v.minutes.low)) {
      return std::unexpected(SubordinateOffsetsError::low_time);
    }
    const auto [high_ok, low_ok] = heights_ok(v.height);
    if (not high_ok) {
      return std::unexpected(SubordinateOffsetsError::high_height);
    }
    if (not low_ok) {
      return std::unexpected(SubordinateOffsetsError::low_height);
    }
    return SubordinateOffsets{
        HighLow<std::chrono::milliseconds>{.high = to_ms(v.minutes.high),
                                           .low = to_ms(v.minutes.low)},
        v.height, v.datum};
  }

  [[nodiscard]] constexpr const HighLow<std::chrono::milliseconds>& time()
      const& noexcept {
    return time_;
  }
  const HighLow<std::chrono::milliseconds>& time() const&& = delete;
  [[nodiscard]] constexpr const HeightAdjustment& height() const& noexcept {
    return height_;
  }
  const HeightAdjustment& height() const&& = delete;
  [[nodiscard]] constexpr const TideDatum& datum() const& noexcept {
    return datum_;
  }
  const TideDatum& datum() const&& = delete;

  friend constexpr bool operator==(const SubordinateOffsets&,
                                   const SubordinateOffsets&) = default;

 private:
  constexpr SubordinateOffsets(HighLow<std::chrono::milliseconds> time,
                               HeightAdjustment height,
                               TideDatum datum) noexcept
      : time_{time}, height_{height}, datum_{datum} {}

  [[nodiscard]] static constexpr bool time_ok(double minutes) noexcept {
    return detail::is_finite(minutes) and minutes >= -max_abs_minutes and
           minutes <= max_abs_minutes;
  }

  struct HeightsOk {
    bool high;
    bool low;
  };
  [[nodiscard]] static constexpr HeightsOk heights_ok(
      const HeightAdjustment& h) noexcept {
    if (std::holds_alternative<HeightRatio>(h)) {
      const auto& r = std::get<HeightRatio>(h).ratio;
      const auto positive = [](double x) {
        return detail::is_finite(x) and x > 0.0;
      };
      return {.high = positive(r.high), .low = positive(r.low)};
    }
    const auto& o = std::get<HeightOffset>(h).offset;
    return {.high = detail::is_finite(o.high.as(LengthUnit::meter)),
            .low = detail::is_finite(o.low.as(LengthUnit::meter))};
  }

  /// minutes x 60,000 rounded half away from zero. |minutes| <= 1440, so
  /// the product is below 2^27 and its fraction is exact.
  [[nodiscard]] static constexpr std::chrono::milliseconds to_ms(
      double minutes) noexcept {
    const double ms = minutes * 60'000.0;
    auto whole = static_cast<std::int64_t>(ms);  // toward zero
    const double fraction = ms - static_cast<double>(whole);
    if (fraction >= 0.5) {
      ++whole;
    } else if (fraction <= -0.5) {
      --whole;
    }
    return std::chrono::milliseconds{whole};
  }

  HighLow<std::chrono::milliseconds> time_;
  HeightAdjustment height_;
  TideDatum datum_;
};

/// The analysed record (docs/harmonics-json.md 8.1): its dates (each may be
/// unknown) and its effective length in years.
struct Record {
  ValidRange dates;
  std::optional<double> years;
  friend constexpr bool operator==(const Record&, const Record&) = default;
};

/// Where a station's data came from and what the builder did with it.
struct Provenance {
  std::optional<std::string> source_record;
  std::vector<std::string> also_in;
  std::vector<ConstituentName> dropped_constituents;
  std::optional<std::string> notes;
  friend bool operator==(const Provenance&, const Provenance&) = default;
};

/// The members every station has (docs/harmonics-json.md 8.1), resolved:
/// `source` is the source its id names. Strings are kept verbatim and are
/// rendered as plain text.
struct StationInfo {
  StationId<provider::Harmonics> id;
  StationText name;
  Location location;
  SourceIndex source;
  std::optional<Licence> licence;  // overrides the source's
  std::optional<std::string> country;
  std::optional<std::string> time_zone;
  Datums datums;
  std::optional<Record> record;
  std::vector<std::string> flags;  // unknown flags kept unchanged
  Provenance provenance;
  friend bool operator==(const StationInfo&, const StationInfo&) = default;
};

}  // namespace mov::core::tide
