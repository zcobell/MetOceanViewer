// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Two stages as one machine (docs/providers-design.md §5.2, §6.3): USGS
// resolution then data, and a CO-OPS catalog's lists then datums.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/fetch/then.hpp"
#include "stage_specs.hpp"
#include "worlds.hpp"

namespace {

using namespace mov::fetch;
using namespace mov::fetch::test;
using std::chrono::seconds;

const mov::core::Time t0 = script_start();
using UsgsThen = Then<UsgsResolveSpec, UsgsBind, UsgsDataSpec>;

template <class T, class Steps>
std::vector<T> all(const Steps& steps) {
  std::vector<T> out;
  for (const auto& s : steps) {
    if (const auto* t = std::get_if<T>(&s)) {
      out.push_back(*t);
    }
  }
  return out;
}

cu::Continuous continuous_request(int days) {
  return {
      .station = *mov::core::StationId<mov::core::provider::Usgs>::make(
          "USGS-02323500"),
      .series = cu::ByParameter{.parameter = *cu::ParameterCode::make("00065")},
      .range = *mov::core::TimeRange::make(t0 - std::chrono::days{days}, t0)};
}

event::Parsed<ResolvePage> resolution(ExchangeId id,
                                      std::vector<cu::SeriesInfo> rows) {
  const std::size_t n = rows.size();
  return parsed_event(
      id, ParseAnswer::data,
      ResolvePage{
          .rows = std::move(rows),
          .summary = {.rows = n, .extent = std::nullopt, .next = std::nullopt}},
      Origin::usgs);
}

event::Parsed<DataPage> data(ExchangeId id, std::int64_t value) {
  return parsed_event(
      id, ParseAnswer::data,
      DataPage{
          .values = {value},
          .summary = {.rows = 1, .extent = std::nullopt, .next = std::nullopt}},
      Origin::usgs);
}

}  // namespace

TEST_CASE("USGS: resolution binds into the data stage", "[fetch][then]") {
  auto started = UsgsThen::start(continuous_request(1500), Options{}, t0);
  auto& m = started.machine;
  const auto resolve = all<Send<UsgsResolveSpec::Target>>(started.steps);
  REQUIRE(resolve.size() == 1);
  auto steps = m.on(
      event::Completed{.id = resolve[0].id, .outcome = Response{.status = 200}},
      t0, Jitter::from_bits(0));
  REQUIRE(all<Parse<UsgsResolveSpec::Target>>(steps).size() == 1);
  steps = m.on(resolution(resolve[0].id, {UsgsWorld::row('a', true),
                                          UsgsWorld::row('b', false)}),
               t0, Jitter::from_bits(0));
  const auto merge = all<Merge<UsgsResolveSpec::Part>>(steps);
  REQUIRE(merge.size() == 1);
  steps = m.on(UsgsWorld::merge(merge[0], 0), t0, Jitter::from_bits(0));
  // 1500 days: two 1000-day windows, ids after stage 1's.
  const auto sends = all<Send<UsgsDataSpec::Target>>(steps);
  REQUIRE(sends.size() == 2);
  CHECK(sends[0].id.value > resolve[0].id.value);
  CHECK(all<Progress>(steps).front() == Progress{.done = 1, .total = 3});
  // A late event of stage 1 is ignored.
  CHECK(m.on(event::Completed{.id = resolve[0].id,
                              .outcome = Response{.status = 200}},
             t0, Jitter::from_bits(0))
            .empty());
  for (const auto& s : sends) {
    static_cast<void>(
        m.on(event::Completed{.id = s.id, .outcome = Response{.status = 200}},
             t0, Jitter::from_bits(0)));
    steps = m.on(data(s.id, s.id.value), t0, Jitter::from_bits(0));
  }
  CHECK(all<Progress>(steps).back() == Progress{.done = 3, .total = 3});
  const auto data_merge = all<Merge<UsgsDataSpec::Part>>(steps);
  REQUIRE(data_merge.size() == 1);
  steps = m.on(UsgsWorld::merge(data_merge[0], 0), t0, Jitter::from_bits(0));
  const auto finish = all<Finish<UsgsDataSpec::Value>>(steps);
  REQUIRE(finish.size() == 1);
  REQUIRE(finish[0].result);
  CHECK(finish[0].result->value.size() == 2);
  CHECK(finish[0].result->trail.stats.exchanges == 3);
  CHECK(m.finished());
}

TEST_CASE("USGS: a bind failure is the fetch's failure", "[fetch][then]") {
  auto started = UsgsThen::start(continuous_request(10), Options{}, t0);
  auto& m = started.machine;
  const auto id = all<Send<UsgsResolveSpec::Target>>(started.steps).at(0).id;
  static_cast<void>(
      m.on(event::Completed{.id = id, .outcome = Response{.status = 200}}, t0,
           Jitter::from_bits(0)));
  auto steps = m.on(resolution(id, {UsgsWorld::row('a', true, "Headwater"),
                                    UsgsWorld::row('b', true, "Tailwater")}),
                    t0, Jitter::from_bits(0));
  steps =
      m.on(UsgsWorld::merge(all<Merge<UsgsResolveSpec::Part>>(steps).at(0), 0),
           t0, Jitter::from_bits(0));
  const auto finish = all<Finish<UsgsDataSpec::Value>>(steps);
  REQUIRE(finish.size() == 1);
  CHECK(
      std::holds_alternative<AmbiguousSeries>(finish[0].result.error().error));
  CHECK(finish[0].result.error().unit ==
        std::optional<UnitLabel>{Resolution{.page = 0}});
}

TEST_CASE("a Cancel during stage 1 never starts stage 2", "[fetch][then]") {
  auto started = UsgsThen::start(continuous_request(10), Options{}, t0);
  auto& m = started.machine;
  const auto steps = m.on(event::Cancel{}, t0, Jitter::from_bits(0));
  const auto finish = all<Finish<UsgsDataSpec::Value>>(steps);
  REQUIRE(finish.size() == 1);
  CHECK(std::holds_alternative<mov::core::Cancelled>(
      finish[0].result.error().error));
  CHECK(all<Abort>(steps).size() == 1);
  CHECK(m.on(event::Cancel{}, t0, Jitter::from_bits(0)).empty());
}

TEST_CASE("the deadline is absolute across the stages", "[fetch][then]") {
  auto started = UsgsThen::start(continuous_request(10),
                                 Options{.deadline = t0 + seconds{10}}, t0);
  auto& m = started.machine;
  const auto id = all<Send<UsgsResolveSpec::Target>>(started.steps).at(0).id;
  static_cast<void>(
      m.on(event::Completed{.id = id, .outcome = Response{.status = 200}}, t0,
           Jitter::from_bits(0)));
  auto steps = m.on(resolution(id, {UsgsWorld::row('a', true)}), t0,
                    Jitter::from_bits(0));
  steps =
      m.on(UsgsWorld::merge(all<Merge<UsgsResolveSpec::Part>>(steps).at(0), 0),
           t0 + seconds{4}, Jitter::from_bits(0));
  // Stage 2 arms the deadline's remainder.
  CHECK(all<Wait>(steps).front().delay == std::chrono::milliseconds{6000});
  steps = m.on(event::WaitElapsed{.id = all<Wait>(steps).front().id},
               t0 + seconds{10}, Jitter::from_bits(0));
  CHECK(std::holds_alternative<DeadlineExceeded>(
      all<Finish<UsgsDataSpec::Value>>(steps).at(0).result.error().error));
}

TEST_CASE("CO-OPS: the station lists bind into one datums unit per station",
          "[fetch][then]") {
  using CoopsThen = Then<CoopsStationsSpec, CoopsBind, CoopsDatumsSpec>;
  auto started = CoopsThen::start(StationsRequest{.lists = 2}, Options{}, t0);
  auto& m = started.machine;
  const auto lists = all<Send<ListUnit>>(started.steps);
  REQUIRE(lists.size() == 2);
  typename CoopsThen::Steps steps;
  for (const auto& s : lists) {
    static_cast<void>(
        m.on(event::Completed{.id = s.id, .outcome = Response{.status = 200}},
             t0, Jitter::from_bits(0)));
    steps =
        m.on(CoopsWorld::parse(Parse<ListUnit>{.id = s.id, .target = s.target},
                               2, ParseAnswer::data),
             t0, Jitter::from_bits(0));
  }
  steps = m.on(
      CoopsWorld::merge(all<Merge<CoopsStationsSpec::Part>>(steps).at(0), 0),
      t0, Jitter::from_bits(0));
  // Lists 0 and 1 named 8700000, 8700001 and 8700001, 8700002.
  CHECK(all<Send<coops::DatumsUnit>>(steps).size() == 3);
}

TEST_CASE("an event of the other stage past the deadline still ends the fetch",
          "[fetch][then][deadline][regression]") {
  auto started = UsgsThen::start(continuous_request(10),
                                 Options{.deadline = t0 + seconds{10}}, t0);
  auto& m = started.machine;
  // A stale stage-2 event at the deadline, while stage 1 runs.
  const auto steps = m.on(
      event::Merged<UsgsDataSpec::Value>{
          .value =
              mov::io::Read<UsgsDataSpec::Value>{.value = {}, .warnings = {}}},
      t0 + seconds{10}, Jitter::from_bits(0));
  const auto finish = all<Finish<UsgsDataSpec::Value>>(steps);
  REQUIRE(finish.size() == 1);
  CHECK(
      std::holds_alternative<DeadlineExceeded>(finish[0].result.error().error));
  CHECK(m.finished());
}
