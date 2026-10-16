// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The worlds protocol_driver.hpp runs machines in: the toy spec (with the
// merge-completeness oracle), USGS resolution then data, and a CO-OPS
// catalog's station lists then datums (both through Then).

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/fetch/machine.hpp"
#include "mov/fetch/then.hpp"
#include "protocol_driver.hpp"
#include "stage_specs.hpp"
#include "toy_spec.hpp"

namespace mov::fetch::test {

/// The error a ParseAnswer stands for.
[[nodiscard]] inline Error parse_error_of(ParseAnswer a, Origin origin) {
  if (a == ParseAnswer::station_or_system) {
    return ServiceError::make(origin, ServiceErrc::station_or_system, "");
  }
  return io::ParseError::make(io::ParseErrc::bad_number, {.line = 1}, "");
}

/// A parsed event for any spec: data, Absent, or an error.
template <class P>
[[nodiscard]] event::Parsed<P> parsed_event(ExchangeId id, ParseAnswer a,
                                            P data, Origin origin) {
  switch (a) {
    case ParseAnswer::data:
      return {.id = id,
              .parsed = io::Read<SlotResult<P>>{
                  .value = SlotResult<P>{std::move(data)}, .warnings = {}}};
    case ParseAnswer::absent:
      return {.id = id,
              .parsed = io::Read<SlotResult<P>>{
                  .value = SlotResult<P>{Absent{}}, .warnings = {}}};
    case ParseAnswer::station_or_system:
    case ParseAnswer::parse_error:
      break;
  }
  return {.id = id, .parsed = std::unexpected{parse_error_of(a, origin)}};
}

// ---- the toy ---------------------------------------------------------------

class ToyWorld {
 public:
  using Machine = fetch::Machine<ToySpec>;
  using Event = fetch::Event<ToySpec>;
  using Step = fetch::Step<ToySpec>;
  using Started = fetch::Started<ToySpec>;
  using Values = std::vector<ToyKey>;
  static constexpr int max_merges = 1;

  [[nodiscard]] static ToyWorld from(ScriptBytes& bytes) {
    const std::uint8_t shape = bytes.take();
    const std::uint8_t more = bytes.take();
    constexpr std::uint8_t origin_count = 5;
    ToyRequest r{.units = 1U + (shape & 3U),
                 .slots = (shape >> 2U) & 3U,  // 0 breaks the contract
                 .depth = (shape >> 4U) & 3U,
                 .branch = 1U + ((shape >> 6U) & 1U),
                 .fallback = ((shape >> 7U) & 1U) != 0,
                 .origin = static_cast<Origin>(more % origin_count),
                 .fail_initial = (more & 0xF0U) == 0xF0U,
                 .fail_next = ((more >> 3U) & 1U) != 0};
    if (r.slots == 3 and (more & 0x40U) == 0) {
      r.slots = 1;  // mostly a valid spec
    }
    return ToyWorld{r};
  }

  explicit ToyWorld(ToyRequest r) : request_{std::move(r)} {}

  [[nodiscard]] Started start(const Options& o, core::Time now) {
    max_attempts_ = o.retry.max_attempts();
    return Machine::start(request_, o, now);
  }

  [[nodiscard]] Event parse(const Parse<ToyTarget>& p, std::uint8_t arg,
                            ParseAnswer a) {
    const bool empty_page = a == ParseAnswer::data and arg % 7 == 6;
    if (empty_page) {
      return parsed_event(p.id, a, ToyPage{}, p.target.origin);
    }
    if (a == ParseAnswer::data) {
      delivered_.insert(key_of(p.target));
    }
    return parsed_event(p.id, a, page_for(p.target), p.target.origin);
  }

  [[nodiscard]] Event merge(const Merge<Values>& m, std::uint8_t arg) const {
    constexpr std::uint8_t fail_one_in = 16;
    if (arg % fail_one_in == 1) {
      return event::Merged<Values>{.value = std::unexpected{Error{NoData{}}}};
    }
    return event::Merged<Values>{.value = ToySpec::merge(m.in_plan_order)};
  }

  [[nodiscard]] static Event stale_merge() {
    return event::Merged<Values>{
        .value = io::Read<Values>{.value = {}, .warnings = {}}};
  }
  [[nodiscard]] static Event stale_parse(ExchangeId id) {
    return parsed_event(id, ParseAnswer::data, ToyPage{}, Origin::ndbc);
  }

  template <class D>
  void on_merge(const Merge<Values>& m, D& d) {
    merged_.clear();
    for (const auto& part : m.in_plan_order) {
      merged_.insert(merged_.end(), part.begin(), part.end());
    }
    if (not std::ranges::is_sorted(merged_) or
        std::ranges::adjacent_find(merged_) != merged_.end()) {
      d.fail("the merge is not in plan order");
    }
  }

  template <class D>
  void on_send(const Send<ToyTarget>& s, D& d) {
    if (++attempts_[key_of(s.target)] > max_attempts_) {
      d.fail("more sends of one slot than max_attempts");
    }
  }

  /// The merge holds exactly the data of the units that neither failed nor
  /// were absent.
  template <class D>
  void on_finish(const Finish<Values>& f, D& d) {
    if (request_.slots == 0 and not request_.fail_initial and not f.result) {
      const auto& e = f.result.error().error;
      if (not std::holds_alternative<UnitWithoutExchanges>(e) and
          not std::holds_alternative<DeadlineExceeded>(e) and
          not std::holds_alternative<core::Cancelled>(e) and
          not std::holds_alternative<ClockOutOfRange>(e)) {
        d.fail("a unit without exchanges did not fail as such");
      }
    }
    if (not f.result) {
      return;
    }
    if (f.result->value != merged_) {
      d.fail("Finish does not hold the merge");
    }
    std::set<std::uint32_t> failed;
    for (const auto& w : f.result->trail.warnings) {
      if (const auto* fw = std::get_if<FetchWarning>(&w)) {
        if (const auto* u = std::get_if<UnitFailed>(fw)) {
          failed.insert(std::get<CatalogPart>(u->unit).index);
        }
      }
    }
    Values expected;
    for (const ToyKey& k : delivered_) {
      if (not failed.contains(label_index(path_of(k)))) {
        expected.push_back(k);
      }
    }
    if (expected != merged_) {
      d.fail("the merge is not the units that neither failed nor were absent");
    }
  }

 private:
  ToyRequest request_;
  std::uint8_t max_attempts_ = 1;
  std::set<ToyKey> delivered_;
  std::map<ToyKey, std::uint32_t> attempts_;
  Values merged_;
};

// ---- USGS through Then -----------------------------------------------------

class UsgsWorld {
 public:
  using Machine = Then<UsgsResolveSpec, UsgsBind, UsgsDataSpec>;
  using Event = Machine::Event;
  using Step = Machine::Step;
  using Started = ThenStarted<UsgsResolveSpec, UsgsBind, UsgsDataSpec>;
  static constexpr int max_merges = 2;

  [[nodiscard]] static UsgsWorld from(ScriptBytes& bytes) {
    const std::uint8_t span_days = bytes.take();
    return UsgsWorld{span_days};
  }

  explicit UsgsWorld(std::uint8_t span_days) : days_{1U + span_days * 9U} {}

  [[nodiscard]] Started start(const Options& o, core::Time now) const {
    const auto range =
        *core::TimeRange::make(now - std::chrono::days{days_}, now);
    return Machine::start(
        cu::Continuous{
            .station =
                *core::StationId<core::provider::Usgs>::make("USGS-02323500"),
            .series =
                cu::ByParameter{.parameter = *cu::ParameterCode::make("00065")},
            .range = range},
        o, now);
  }

  [[nodiscard]] static cu::SeriesInfo row(
      char id, bool primary, std::optional<std::string_view> sublocation = {}) {
    cu::SeriesInfo out{.id = *cu::TimeSeriesId::make(std::string(32, id)),
                       .parameter = *cu::ParameterCode::make("00065"),
                       .statistic = cu::StatisticId::instantaneous(),
                       .parameter_name = "gage height",
                       .primary = primary,
                       .unit = std::nullopt,
                       .validity = {}};
    if (sublocation) {
      out.sublocation = *cu::Sublocation::make(std::string{*sublocation});
    }
    return out;
  }

  [[nodiscard]] Event parse(const Parse<UsgsResolveSpec::Target>& p,
                            std::uint8_t arg, ParseAnswer a) const {
    std::vector<cu::SeriesInfo> rows;
    switch (arg % 5) {
      case 0:
      case 1:
        rows = {row('a', true), row('b', false)};
        break;
      case 2:
        // Two sublocations, none without one: ambiguous.
        rows = {row('a', true, "Headwater"), row('b', true, "Tailwater")};
        break;
      case 3:
        rows = {row('c', false)};  // no Primary
        break;
      default:
        break;  // empty
    }
    const auto taken = p.target.page.guard.taken();
    std::optional<Link> next;
    if (arg % 3 == 1) {
      next = "r" + std::to_string(arg % 2 == 0 ? 0U : taken);
    }
    return parsed_event(p.id, a,
                        ResolvePage{.rows = rows,
                                    .summary = {.rows = rows.size(),
                                                .extent = std::nullopt,
                                                .next = next}},
                        Origin::usgs);
  }

  [[nodiscard]] Event parse(const Parse<UsgsDataSpec::Target>& p,
                            std::uint8_t arg, ParseAnswer a) const {
    const auto& unit = p.target;
    const auto page = unit.page.guard.taken();
    const core::Time first =
        unit.window.begin() + (std::chrono::minutes{2} * page) -
        (arg % 11 == 10 ? std::chrono::minutes{5} : std::chrono::minutes{0});
    std::optional<Link> next;
    if (arg % 3 == 1) {
      next = "d" + std::to_string(arg % 4 == 1 ? 0U : page);
    }
    return parsed_event(
        p.id, a,
        DataPage{.values = {first.time_since_epoch().count()},
                 .summary = {.rows = 1,
                             .extent =
                                 Extent<core::Time>{
                                     .first = first,
                                     .last = first + std::chrono::minutes{1}},
                             .next = next}},
        Origin::usgs);
  }

  [[nodiscard]] static Event merge(const Merge<UsgsResolveSpec::Part>& m,
                                   std::uint8_t /*arg*/) {
    return event::Merged<UsgsResolveSpec::Value>{
        .value = UsgsResolveSpec::merge(m.in_plan_order)};
  }
  [[nodiscard]] static Event merge(const Merge<UsgsDataSpec::Part>& m,
                                   std::uint8_t /*arg*/) {
    return event::Merged<UsgsDataSpec::Value>{
        .value = UsgsDataSpec::merge(m.in_plan_order)};
  }
  [[nodiscard]] static Event stale_merge() {
    return event::Merged<UsgsDataSpec::Value>{
        .value = io::Read<UsgsDataSpec::Value>{.value = {}, .warnings = {}}};
  }
  [[nodiscard]] static Event stale_parse(ExchangeId id) {
    return parsed_event(id, ParseAnswer::absent, DataPage{}, Origin::usgs);
  }

  template <class M, class D>
  void on_merge(const M& /*m*/, D& /*d*/) {}
  template <class S, class D>
  void on_send(const S& /*s*/, D& /*d*/) {}
  template <class D>
  void on_finish(const Finish<UsgsDataSpec::Value>& f, D& d) const {
    if (f.result and f.result->value.empty()) {
      d.fail("a USGS success without data");
    }
  }

 private:
  std::uint32_t days_;
};

// ---- CO-OPS catalog through Then -------------------------------------------

class CoopsWorld {
 public:
  using Machine = Then<CoopsStationsSpec, CoopsBind, CoopsDatumsSpec>;
  using Event = Machine::Event;
  using Step = Machine::Step;
  using Started = ThenStarted<CoopsStationsSpec, CoopsBind, CoopsDatumsSpec>;
  static constexpr int max_merges = 2;

  [[nodiscard]] static CoopsWorld from(ScriptBytes& bytes) {
    return CoopsWorld{1U + (bytes.take() % 3U)};
  }

  explicit CoopsWorld(std::uint32_t lists) : lists_{lists} {}

  [[nodiscard]] Started start(const Options& o, core::Time now) const {
    return Machine::start(StationsRequest{.lists = lists_}, o, now);
  }

  [[nodiscard]] static Event parse(const Parse<ListUnit>& p, std::uint8_t arg,
                                   ParseAnswer a) {
    StationsPage page;
    for (std::uint32_t i = 0; i < arg % 4U; ++i) {
      page.stations.push_back(
          *CoopsId::make(std::to_string(8'700'000U + p.target.index + i)));
    }
    return parsed_event(p.id, a, std::move(page), Origin::coops_metadata);
  }
  [[nodiscard]] static Event parse(const Parse<coops::DatumsUnit>& p,
                                   std::uint8_t /*arg*/, ParseAnswer a) {
    return parsed_event(p.id, a, DatumsPage{.index = p.target.index},
                        Origin::coops_metadata);
  }
  [[nodiscard]] static Event merge(const Merge<CoopsStationsSpec::Part>& m,
                                   std::uint8_t /*arg*/) {
    return event::Merged<CoopsStationsSpec::Value>{
        .value = CoopsStationsSpec::merge(m.in_plan_order)};
  }
  [[nodiscard]] static Event merge(const Merge<CoopsDatumsSpec::Part>& m,
                                   std::uint8_t /*arg*/) {
    return event::Merged<CoopsDatumsSpec::Value>{
        .value = CoopsDatumsSpec::merge(m.in_plan_order)};
  }
  [[nodiscard]] static Event stale_merge() {
    return event::Merged<CoopsStationsSpec::Value>{
        .value =
            io::Read<CoopsStationsSpec::Value>{.value = {}, .warnings = {}}};
  }
  [[nodiscard]] static Event stale_parse(ExchangeId id) {
    return parsed_event(id, ParseAnswer::data, DatumsPage{.index = 0},
                        Origin::coops_metadata);
  }

  template <class M, class D>
  void on_merge(const M& /*m*/, D& /*d*/) {}
  template <class S, class D>
  void on_send(const S& /*s*/, D& /*d*/) {}
  template <class D>
  void on_finish(const Finish<CoopsDatumsSpec::Value>& f, D& d) const {
    if (f.result and not std::ranges::is_sorted(f.result->value)) {
      d.fail("the datums are not in station order");
    }
  }

 private:
  std::uint32_t lists_;
};

}  // namespace mov::fetch::test
