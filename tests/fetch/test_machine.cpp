// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The fetch machine over scripted events (docs/providers-design.md §5.5,
// §5.6), with the toy spec of toy_spec.hpp.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "machine_script.hpp"
#include "mov/fetch/machine.hpp"
#include "toy_spec.hpp"

namespace {

using namespace mov::fetch;
using mov::fetch::test::page_for;
using mov::fetch::test::Script;
using mov::fetch::test::t0;
using mov::fetch::test::ToyKey;
using mov::fetch::test::ToyRequest;
using mov::fetch::test::ToySpec;
using std::chrono::milliseconds;
using std::chrono::seconds;
using Values = std::vector<ToyKey>;
using ToySend = Send<ToySpec::Target>;

constexpr Jitter half = Jitter::from_bits(std::uint64_t{1} << 63U);

/// Answers every send with 200 and parses it.
template <class Sc>
void accept_all(Sc& sc, const std::vector<ToySend>& sends) {
  for (const auto& s : sends) {
    sc.respond(s.id, 200);
    REQUIRE(sc.parses().size() == 1);
    sc.parsed(s.id, page_for(s.target));
  }
}

Values concat(const std::vector<Values>& parts) {
  Values out;
  for (const auto& p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}

template <class T>
std::vector<T> fetch_warnings(const Trail& trail) {
  std::vector<T> out;
  for (const auto& w : trail.warnings) {
    if (const auto* f = std::get_if<FetchWarning>(&w)) {
      if (const auto* t = std::get_if<T>(f)) {
        out.push_back(*t);
      }
    }
  }
  return out;
}

UnitLabel label(std::uint32_t index) { return CatalogPart{.index = index}; }

}  // namespace

TEST_CASE("start plans the units and sends one exchange per slot",
          "[fetch][machine]") {
  Script<ToySpec> sc{ToyRequest{.units = 3, .slots = 2}, Options{}};
  const auto sends = sc.sends();
  REQUIRE(sends.size() == 6);
  CHECK(sc.all<Progress>() == std::vector<Progress>{{.done = 0, .total = 3}});
  for (std::size_t i = 1; i < sends.size(); ++i) {
    CHECK(sends[i - 1].id < sends[i].id);
  }
  CHECK(sends.front().priority == Priority::interactive);
  CHECK(sends.front().origin == Origin::coops_data);
  CHECK_FALSE(sc.machine().finished());
}

TEST_CASE("a fetch whose parts arrive out of order merges them in plan order",
          "[fetch][machine]") {
  Script<ToySpec> sc{ToyRequest{.units = 3}, Options{}};
  const auto sends = sc.sends();
  REQUIRE(sends.size() == 3);
  sc.respond(sends[2].id, 200, 10);
  sc.respond(sends[0].id, 200, 10);
  sc.respond(sends[1].id, 200, 10);
  sc.parsed(sends[2].id, page_for(sends[2].target));
  CHECK(sc.all<Progress>() == std::vector<Progress>{{.done = 1, .total = 3}});
  sc.parsed(sends[0].id, page_for(sends[0].target),
            {mov::io::Warning{.code = mov::io::WarningCode::times_reordered}});
  sc.parsed(sends[1].id, page_for(sends[1].target));
  const Values merged = concat(sc.merge().in_plan_order);
  CHECK(std::ranges::is_sorted(merged));
  CHECK(merged.size() == 3);

  sc.merged(merged,
            {mov::io::Warning{.code = mov::io::WarningCode::epoch_used}});
  const auto result = sc.finish().result;
  REQUIRE(result);
  CHECK(result->value == merged);
  CHECK(result->trail.stats ==
        Stats{.exchanges = 3, .retries = 0, .bytes = 30});
  // Unit warnings in plan order, then the merge's.
  REQUIRE(result->trail.warnings.size() == 2);
  CHECK(std::get<mov::io::Warning>(result->trail.warnings[0]).code ==
        mov::io::WarningCode::times_reordered);
  CHECK(std::get<mov::io::Warning>(result->trail.warnings[1]).code ==
        mov::io::WarningCode::epoch_used);
  CHECK(sc.machine().finished());
  CHECK(sc.cancel().empty());
  CHECK(sc.merged(merged).empty());
}

TEST_CASE("events naming unknown or settled ids cause no steps",
          "[fetch][machine]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto send = sc.sends().at(0);
  CHECK(sc.respond(ExchangeId{.value = 999}, 200).empty());
  CHECK(sc.elapse(TimerId{.value = 999}).empty());
  CHECK(sc.parsed(send.id, test::ToyPage{}).empty());  // not completed yet
  CHECK(sc.merged(Values{}).empty());                  // no Merge step yet
  sc.respond(send.id, 200);
  CHECK(sc.respond(send.id, 200).empty());  // completed twice
}

TEST_CASE("a retried exchange gets a fresh id and the stale one is ignored",
          "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto first = sc.sends().at(0);
  sc.set_jitter(half);
  sc.respond(first.id, 503);
  const auto wait = sc.first<Wait>();
  CHECK(wait.delay == milliseconds{500});  // half of base * 2^0
  sc.advance(wait.delay);
  sc.elapse(wait.id);
  const auto second = sc.sends().at(0);
  CHECK(second.id != first.id);
  CHECK(second.target == first.target);
  CHECK(sc.respond(first.id, 200).empty());  // the old attempt's late answer
  sc.respond(second.id, 200);
  sc.parsed(second.id, page_for(second.target));
  sc.merged(concat(sc.merge().in_plan_order));
  const auto result = sc.finish().result;
  REQUIRE(result);
  CHECK(result->trail.stats.exchanges == 2);
  CHECK(result->trail.stats.retries == 1);
  CHECK(fetch_warnings<Retried>(result->trail) ==
        std::vector<Retried>{{.unit = label(1), .attempts = 2}});
}

TEST_CASE("a timer that fires early waits again", "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  sc.set_jitter(half);
  sc.respond(sc.sends().at(0).id, 503);
  const auto wait = sc.first<Wait>();
  REQUIRE(wait.delay == milliseconds{500});
  // Qt's coarse timers may fire up to 5 % early.
  sc.advance(milliseconds{400});
  sc.elapse(wait.id);
  CHECK(sc.sends().empty());
  const auto again = sc.first<Wait>();
  CHECK(again.delay == milliseconds{100});
  CHECK(again.id != wait.id);
  sc.advance(milliseconds{100});
  sc.elapse(again.id);
  CHECK(sc.sends().size() == 1);
}

TEST_CASE("an exchange that keeps failing fails the strict fetch with its unit",
          "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 2}, Options{}};
  const auto sends = sc.sends();
  const ExchangeId other = sends[1].id;
  // Attempts 1 to 3 fail and are retried; the 4th fails the fetch.
  const auto fail_and_retry = [&sc](ExchangeId failed) {
    sc.respond(failed, 500);
    const auto wait = sc.first<Wait>();
    sc.advance(wait.delay);
    sc.elapse(wait.id);
    return sc.first<ToySend>().id;
  };
  const ExchangeId last =
      fail_and_retry(fail_and_retry(fail_and_retry(sends[0].id)));
  sc.respond(last, 500);
  CHECK(sc.all<Abort>() ==
        std::vector<Abort>{{.exchanges = {other}, .parses = {}}});
  CHECK(sc.finish().result.error() ==
        Failure{.error = HttpError{.status = 500, .origin = Origin::coops_data},
                .unit = label(1)});
  CHECK(sc.respond(other, 200).empty());
}

TEST_CASE("transport failures retry or fail by their classification",
          "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  sc.complete(sc.sends().at(0).id, TransportErrc::inactivity_timeout);
  sc.elapse(sc.first<Wait>().id);
  sc.complete(sc.sends().at(0).id, TransportErrc::tls);
  CHECK(sc.finish().result.error().error ==
        Error{TransportError{.code = TransportErrc::tls,
                             .origin = Origin::coops_data}});
}

TEST_CASE("a 429 cools the origin down and holds its other sends",
          "[fetch][machine][cool-down]") {
  Script<ToySpec> sc{ToyRequest{.units = 2}, Options{}};
  const auto sends = sc.sends();
  sc.complete(sends[0].id, Response{.status = 429, .retry_after = seconds{5}});
  CHECK(sc.all<CoolDown>() ==
        std::vector<CoolDown>{
            {.origin = Origin::coops_data, .for_ = milliseconds{5000}}});
  const auto retry = sc.first<Wait>();
  CHECK(retry.delay == milliseconds{5000});
  // The other unit fails transiently 1 s later; its retry may not go out
  // before the cool-down ends.
  sc.advance(seconds{1});
  sc.respond(sends[1].id, 503);
  const auto backoff_wait = sc.first<Wait>();
  CHECK(backoff_wait.delay == milliseconds{0});  // zero jitter
  sc.elapse(backoff_wait.id);
  CHECK(sc.sends().empty());
  const auto held = sc.first<Wait>();
  CHECK(held.delay == milliseconds{4000});
  sc.advance(held.delay);
  sc.elapse(held.id);
  REQUIRE(sc.sends().size() == 1);
  sc.elapse(retry.id);
  REQUIRE(sc.sends().size() == 1);
  CHECK(sc.sends()[0].target == sends[0].target);
}

TEST_CASE("a 429 that is not retried still cools the origin down",
          "[fetch][machine][cool-down][regression]") {
  SECTION("Retry-After beyond the policy") {
    Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
    sc.complete(sc.sends().at(0).id,
                Response{.status = 429, .retry_after = seconds{121}});
    CHECK(sc.all<CoolDown>() ==
          std::vector<CoolDown>{
              {.origin = Origin::coops_data, .for_ = seconds{121}}});
    CHECK(sc.finish().result.error().error ==
          Error{RateLimited{.origin = Origin::coops_data,
                            .retry_after = seconds{121}}});
  }
  SECTION("attempts exhausted") {
    const RetryPolicy one = *RetryPolicy::make({.max_attempts = 1});
    Script<ToySpec> sc{ToyRequest{.units = 1}, Options{.retry = one}};
    sc.complete(sc.sends().at(0).id,
                Response{.status = 429, .retry_after = seconds{3}});
    CHECK(sc.all<CoolDown>() ==
          std::vector<CoolDown>{
              {.origin = Origin::coops_data, .for_ = seconds{3}}});
    CHECK(sc.finish().result.error().error ==
          Error{RateLimited{.origin = Origin::coops_data,
                            .retry_after = seconds{3}}});
  }
  SECTION("a cool-down is never shorter than min_cool_down") {
    Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
    sc.complete(sc.sends().at(0).id,
                Response{.status = 429, .retry_after = seconds{0}});
    CHECK(sc.first<CoolDown>().for_ == min_cool_down);
    CHECK(sc.first<Wait>().delay == min_cool_down);
  }
  SECTION("a Retry-After of any size is carried without overflow") {
    Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
    sc.complete(
        sc.sends().at(0).id,
        Response{.status = 429, .retry_after = std::chrono::seconds::max()});
    CHECK(sc.first<CoolDown>().for_ == clock_limit);
    CHECK_FALSE(sc.finish().result.has_value());
  }
}

TEST_CASE("a send held longer than max_retry_after fails as RateLimited",
          "[fetch][machine][cool-down][regression]") {
  Script<ToySpec> sc{ToyRequest{.units = 2},
                     Options{.strictness = Strictness::lenient}};
  const auto sends = sc.sends();
  sc.complete(sends[0].id,
              Response{.status = 429, .retry_after = seconds{300}});
  sc.respond(sends[1].id, 503);
  sc.elapse(sc.first<Wait>().id);
  CHECK(sc.sends().empty());
  CHECK_FALSE(sc.has<Wait>());
  const auto result = sc.finish().result;
  REQUIRE_FALSE(result);
  // Both units failed: the first unit's error.
  CHECK(result.error().unit == std::optional<UnitLabel>{label(1)});
}

TEST_CASE("an exhausted rate limit cools down without a retry",
          "[fetch][machine][cool-down]") {
  SECTION("for the response's Retry-After") {
    Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
    sc.complete(sc.sends().at(0).id, Response{.status = 200,
                                              .retry_after = seconds{7},
                                              .rate_remaining = 0});
    CHECK(sc.first<CoolDown>().for_ == seconds{7});
    CHECK(sc.parses().size() == 1);
    CHECK_FALSE(sc.has<Wait>());
  }
  SECTION("for the policy's base without one; a negative count too") {
    Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
    sc.complete(sc.sends().at(0).id,
                Response{.status = 200, .rate_remaining = -3});
    CHECK(sc.first<CoolDown>().for_ == RetryPolicy::defaults().base());
  }
}

TEST_CASE("a lenient fetch turns a failed unit into a gap and a warning",
          "[fetch][machine][lenient]") {
  Script<ToySpec> sc{ToyRequest{.units = 3},
                     Options{.strictness = Strictness::lenient}};
  const auto sends = sc.sends();
  sc.respond(sends[1].id, 404);  // not NDBC: a failure
  CHECK_FALSE(sc.finished_now());
  accept_all(sc, {sends[0], sends[2]});
  const Values merged = concat(sc.merge().in_plan_order);
  CHECK(merged.size() == 2);
  sc.merged(merged);
  const auto result = sc.finish().result;
  REQUIRE(result);
  CHECK(fetch_warnings<UnitFailed>(result->trail) ==
        std::vector<UnitFailed>{
            {.unit = label(2),
             .error = HttpError{.status = 404, .origin = Origin::coops_data}}});
}

TEST_CASE(
    "a lenient fetch in which every unit failed is the first unit's failure",
    "[fetch][machine][lenient]") {
  Script<ToySpec> sc{ToyRequest{.units = 2},
                     Options{.strictness = Strictness::lenient}};
  const auto sends = sc.sends();
  sc.respond(sends[1].id, 400);
  sc.complete(sends[0].id, TransportErrc::host_not_found);
  CHECK(sc.finish().result.error() ==
        Failure{.error = TransportError{.code = TransportErrc::host_not_found,
                                        .origin = Origin::coops_data},
                .unit = label(1)});
}

TEST_CASE("a lenient unit that fails drops its other exchanges and parses",
          "[fetch][machine][lenient]") {
  Script<ToySpec> sc{ToyRequest{.units = 2, .slots = 3},
                     Options{.strictness = Strictness::lenient}};
  const auto sends = sc.sends();
  sc.respond(sends[1].id, 200);  // parsing
  sc.respond(sends[0].id, 400);
  CHECK(sc.all<Abort>() == std::vector<Abort>{{.exchanges = {sends[2].id},
                                               .parses = {sends[1].id}}});
  CHECK(sc.respond(sends[2].id, 200).empty());
  CHECK(sc.parsed(sends[1].id, page_for(sends[1].target)).empty());
  accept_all(sc, {sends[3], sends[4], sends[5]});
  CHECK(concat(sc.merge().in_plan_order).size() == 3);
}

TEST_CASE("a lenient unit failure keeps its io warnings and retries",
          "[fetch][machine][lenient]") {
  Script<ToySpec> sc{ToyRequest{.units = 2, .slots = 2},
                     Options{.strictness = Strictness::lenient}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 503);
  sc.elapse(sc.first<Wait>().id);
  const auto retry = sc.sends().at(0);
  sc.respond(retry.id, 200);
  sc.parsed(retry.id, page_for(retry.target),
            {mov::io::Warning{.code = mov::io::WarningCode::times_reordered}});
  sc.respond(sends[1].id, 400);  // the unit fails
  accept_all(sc, {sends[2], sends[3]});
  sc.merged(concat(sc.merge().in_plan_order));
  const auto result = sc.finish().result;
  REQUIRE(result);
  REQUIRE(result->trail.warnings.size() == 3);
  CHECK(std::get<mov::io::Warning>(result->trail.warnings[0]).code ==
        mov::io::WarningCode::times_reordered);
  CHECK(fetch_warnings<Retried>(result->trail) ==
        std::vector<Retried>{{.unit = label(1), .attempts = 2}});
  CHECK(fetch_warnings<UnitFailed>(result->trail).size() == 1);
}

TEST_CASE("no data anywhere is NoData, decided at the end",
          "[fetch][machine][no-data]") {
  Script<ToySpec> sc{ToyRequest{.units = 2, .origin = Origin::ndbc}, Options{}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 404);  // NDBC: absent
  CHECK_FALSE(sc.finished_now());
  sc.respond(sends[1].id, 200);
  sc.parsed_absent(sends[1].id);  // "No data was found"
  CHECK(sc.finish().result.error() ==
        Failure{.error = NoData{}, .unit = std::nullopt});
}

TEST_CASE("empty parts are gaps, not data", "[fetch][machine][no-data]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto id = sc.sends().at(0).id;
  sc.respond(id, 200);
  sc.parsed(id, test::ToyPage{});
  CHECK(sc.finish().result.error().error == Error{NoData{}});
}

TEST_CASE("a paired unit keeps its data when one half is absent",
          "[fetch][machine][paired]") {
  Script<ToySpec> sc{ToyRequest{.units = 1, .slots = 2, .fallback = true},
                     Options{}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 200);
  sc.parsed_absent(sends[0].id);  // the observed half has no data
  sc.respond(sends[1].id, 200);
  sc.parsed(sends[1].id, page_for(sends[1].target));
  CHECK(sc.sends().empty());  // not Absent: no fallback
  const Values merged = concat(sc.merge().in_plan_order);
  CHECK(merged == Values{test::key_of(sends[1].target)});
  sc.merged(merged);
  const auto result = sc.finish().result;
  REQUIRE(result);
  CHECK(fetch_warnings<PartlyAbsent>(result->trail) ==
        std::vector<PartlyAbsent>{{.unit = label(1), .slot = 0}});
}

TEST_CASE("a unit with every slot absent falls back",
          "[fetch][machine][chain]") {
  Script<ToySpec> sc{
      ToyRequest{
          .units = 1, .slots = 2, .fallback = true, .origin = Origin::ndbc},
      Options{}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 404);
  sc.respond(sends[1].id, 404);
  const auto fallback = sc.sends();
  REQUIRE(fallback.size() == 2);
  CHECK(fallback[0].target.path == test::ToyPath{0, 0});
  accept_all(sc, fallback);
  CHECK(concat(sc.merge().in_plan_order) ==
        Values{test::key_of(fallback[0].target),
               test::key_of(fallback[1].target)});
}

TEST_CASE("a strict parse error fails the fetch with the unit",
          "[fetch][machine]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto id = sc.sends().at(0).id;
  sc.respond(id, 200);
  const Error error = mov::io::ParseError::make(mov::io::ParseErrc::bad_number,
                                                {.line = 3}, "x");
  sc.parse_failed(id, error);
  CHECK(sc.finish().result.error() ==
        Failure{.error = error, .unit = label(1)});
}

TEST_CASE("station_or_system is retried once, then fails",
          "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const Error ambiguous =
      ServiceError::make(Origin::coops_data, ServiceErrc::station_or_system,
                         "not a valid station");
  auto id = sc.sends().at(0).id;
  sc.respond(id, 200);
  sc.parse_failed(id, ambiguous);
  sc.elapse(sc.first<Wait>().id);
  id = sc.sends().at(0).id;
  sc.respond(id, 200);
  sc.parse_failed(id, ambiguous);
  CHECK(sc.finish().result.error().error == ambiguous);
}

TEST_CASE("api_key_invalid fails at once", "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 1, .origin = Origin::usgs}, Options{}};
  const auto id = sc.sends().at(0).id;
  sc.respond(id, 200);
  sc.parse_failed(
      id, ServiceError::make(Origin::usgs, ServiceErrc::api_key_invalid, ""));
  CHECK_FALSE(sc.has<Wait>());
  CHECK(sc.finished_now());
}

TEST_CASE("cancel aborts the exchanges and parses in flight",
          "[fetch][machine][cancel]") {
  Script<ToySpec> sc{ToyRequest{.units = 2}, Options{}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 200);  // parsing
  sc.cancel();
  CHECK(sc.all<Abort>() == std::vector<Abort>{{.exchanges = {sends[1].id},
                                               .parses = {sends[0].id}}});
  CHECK(sc.finish().result.error() ==
        Failure{.error = mov::core::Cancelled{}, .unit = std::nullopt});
  CHECK(sc.steps().size() == 2);
  CHECK(sc.parsed(sends[0].id, page_for(sends[0].target)).empty());
}

TEST_CASE("cancel during the merge finishes Cancelled",
          "[fetch][machine][cancel]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  accept_all(sc, sc.sends());
  CHECK(sc.has<Merge<Values>>());
  sc.cancel();
  CHECK(sc.finish().result.error().error == Error{mov::core::Cancelled{}});
  CHECK(sc.merged(Values{{1}}).empty());
}

TEST_CASE("a failed merge is the fetch's failure", "[fetch][machine]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  accept_all(sc, sc.sends());
  sc.on(event::Merged<Values>{.value = std::unexpected{Error{NoData{}}}});
  CHECK(sc.finish().result.error() ==
        Failure{.error = NoData{}, .unit = std::nullopt});
}

TEST_CASE("the deadline aborts everything and finishes DeadlineExceeded",
          "[fetch][machine][deadline]") {
  Script<ToySpec> sc{ToyRequest{.units = 2},
                     Options{.deadline = t0 + seconds{10}}};
  const auto timer = sc.first<Wait>();
  CHECK(timer.delay == milliseconds{10000});
  sc.advance(seconds{4});
  sc.elapse(timer.id);
  const auto rearmed = sc.first<Wait>();
  CHECK(rearmed.delay == milliseconds{6000});
  CHECK_FALSE(sc.finished_now());
  sc.advance(seconds{6});
  sc.elapse(rearmed.id);
  REQUIRE(sc.all<Abort>().size() == 1);
  CHECK(sc.all<Abort>()[0].exchanges.size() == 2);
  CHECK(sc.finish().result.error().error == Error{DeadlineExceeded{}});
}

TEST_CASE("tick applies the clock alone", "[fetch][machine][deadline]") {
  Script<ToySpec> sc{ToyRequest{.units = 1},
                     Options{.deadline = t0 + seconds{10}}};
  CHECK(sc.machine().tick(t0 + seconds{5}).empty());
  const auto steps = sc.machine().tick(t0 + seconds{10});
  REQUIRE(steps.size() == 2);  // the Abort of the exchange, then Finish
  CHECK(std::holds_alternative<Finish<Values>>(steps.back()));
  CHECK(sc.machine().tick(t0 + seconds{20}).empty());
}

TEST_CASE("any event past the deadline finishes the fetch",
          "[fetch][machine][deadline]") {
  Script<ToySpec> sc{ToyRequest{.units = 1},
                     Options{.deadline = t0 + seconds{10}}};
  const auto id = sc.sends().at(0).id;
  sc.advance(seconds{11});
  sc.respond(id, 200);
  CHECK(sc.finish().result.error().error == Error{DeadlineExceeded{}});
}

TEST_CASE("a deadline already passed finishes at start",
          "[fetch][machine][deadline]") {
  const Script<ToySpec> sc{ToyRequest{.units = 1}, Options{.deadline = t0}};
  REQUIRE(sc.steps().size() == 1);
  CHECK(sc.finish().result.error().error == Error{DeadlineExceeded{}});
  CHECK(sc.machine().finished());
}

TEST_CASE("the clock: monotonic feed, bounded values",
          "[fetch][machine][clock][regression]") {
  SECTION("an event earlier than one seen is taken as the latest") {
    Script<ToySpec> sc{ToyRequest{.units = 2}, Options{}};
    const auto sends = sc.sends();
    sc.advance(seconds{10});
    sc.complete(sends[0].id,
                Response{.status = 429, .retry_after = seconds{5}});
    sc.advance(-seconds{10});  // back to t0
    sc.respond(sends[1].id, 503);
    sc.elapse(sc.first<Wait>().id);
    // Held until t0 + 15 s, measured from t0 + 10 s.
    CHECK(sc.first<Wait>().delay == milliseconds{5000});
  }
  SECTION("a start or deadline beyond ±10,000 years is refused") {
    const mov::core::Time far{std::chrono::years{20'000}};
    const Script<ToySpec> late{ToyRequest{.units = 1}, Options{}, far};
    CHECK(late.finish().result.error().error ==
          Error{ClockOutOfRange{.time = far}});
    const Script<ToySpec> deadline{ToyRequest{.units = 1},
                                   Options{.deadline = mov::core::Time::max()}};
    CHECK(deadline.finish().result.error().error ==
          Error{ClockOutOfRange{.time = mov::core::Time::max()}});
  }
  SECTION("an event time beyond the range is clamped") {
    Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
    const auto id = sc.sends().at(0).id;
    sc.advance(milliseconds{std::numeric_limits<std::int64_t>::max() / 2});
    sc.respond(id, 503);
    CHECK(sc.has<Wait>());
  }
}

TEST_CASE("a failing plan finishes at start", "[fetch][machine]") {
  const Script<ToySpec> sc{ToyRequest{.fail_initial = true}, Options{}};
  CHECK(sc.finish().result.error().error == Error{NoData{}});
}

TEST_CASE("no units is NoData", "[fetch][machine][no-data]") {
  const Script<ToySpec> sc{ToyRequest{.units = 0}, Options{}};
  CHECK(sc.finish().result.error().error == Error{NoData{}});
}

TEST_CASE("a unit without exchanges breaks the spec's contract",
          "[fetch][machine][regression]") {
  const Script<ToySpec> sc{ToyRequest{.units = 2, .slots = 0, .depth = 2},
                           Options{}};
  CHECK(sc.sends().empty());
  CHECK(sc.finish().result.error() ==
        Failure{.error = UnitWithoutExchanges{}, .unit = label(1)});
  const Script<ToySpec> lenient{ToyRequest{.units = 2, .slots = 0},
                                Options{.strictness = Strictness::lenient}};
  CHECK(lenient.finish().result.error().error == Error{UnitWithoutExchanges{}});
}

TEST_CASE("max_units caps the plan", "[fetch][machine][regression]") {
  const Script<ToySpec> initial{ToyRequest{.units = 3},
                                Options{.max_units = 2}};
  CHECK(initial.finish().result.error().error == Error{UnitLimit{.max = 2}});
  Script<ToySpec> chain{ToyRequest{.units = 1, .depth = 1, .branch = 2},
                        Options{.max_units = 2}};
  const auto s = chain.sends().at(0);
  chain.respond(s.id, 200);
  chain.parsed(s.id, page_for(s.target));
  CHECK(chain.finish().result.error() ==
        Failure{.error = UnitLimit{.max = 2}, .unit = label(1)});
}

TEST_CASE("next adds units: successors follow their unit, each sent once",
          "[fetch][machine][chain][regression]") {
  Script<ToySpec> sc{ToyRequest{.units = 2, .depth = 2, .branch = 2},
                     Options{}};
  std::vector<Progress> progress;
  std::set<test::ToyPath> sent;
  std::vector<ToySend> queue = sc.sends();
  while (not queue.empty()) {
    const auto s = queue.back();
    queue.pop_back();
    CHECK(sent.insert(s.target.path).second);
    sc.respond(s.id, 200);
    sc.parsed(s.id, page_for(s.target));
    for (const auto& p : sc.all<Progress>()) {
      progress.push_back(p);
    }
    for (const auto& n : sc.sends()) {
      queue.push_back(n);
    }
  }
  CHECK(sent.size() == 14);  // 2 * (1 + 2 + 4)
  CHECK(progress.back() == Progress{.done = 14, .total = 14});
  CHECK(std::ranges::is_sorted(progress, {}, &Progress::done));
  const Values merged = concat(sc.merge().in_plan_order);
  CHECK(merged.size() == 14);
  CHECK(std::ranges::is_sorted(merged));
}

TEST_CASE("a next that fails: strict fails, lenient keeps the data",
          "[fetch][machine][chain]") {
  const auto run = [](Strictness strictness) {
    Script<ToySpec> sc{ToyRequest{.units = 1, .depth = 3, .fail_next = true},
                       Options{.strictness = strictness}};
    auto s = sc.sends().at(0);
    sc.respond(s.id, 200);
    sc.parsed(s.id, page_for(s.target));
    s = sc.sends().at(0);
    sc.respond(s.id, 200);
    sc.parsed(s.id, page_for(s.target));
    return sc;
  };
  const auto strict = run(Strictness::strict);
  CHECK(strict.finish().result.error() ==
        Failure{
            .error = PagingError{.rule = PagingRule::link_repeated, .page = 1},
            .unit = label(5)});
  auto lenient = run(Strictness::lenient);
  const Values merged = concat(lenient.merge().in_plan_order);
  CHECK(merged.size() == 2);  // the page that broke the rule is kept
  lenient.merged(merged);
  const auto result = lenient.finish().result;
  REQUIRE(result);
  CHECK(fetch_warnings<ChainStopped>(result->trail) ==
        std::vector<ChainStopped>{
            {.unit = label(5),
             .error =
                 PagingError{.rule = PagingRule::link_repeated, .page = 1}}});
}

TEST_CASE("a second stage continues the first's ids and trail",
          "[fetch][machine][stages]") {
  Script<ToySpec> first{ToyRequest{.units = 1}, Options{}};
  accept_all(first, first.sends());
  first.merged(Values{{7}});
  const auto fetched = first.finish().result;
  REQUIRE(fetched);

  Script<ToySpec> second{ToyRequest{.units = 2},
                         Options{.priority = Priority::background,
                                 .ids = first.machine().next_ids()}};
  const auto sends = second.sends();
  REQUIRE(sends.size() == 2);
  CHECK(sends[0].id.value >= 1);
  CHECK(sends[0].priority == Priority::background);
  accept_all(second, sends);
  second.merged(Values{{1}, {2}});
  const auto later = second.finish().result;
  REQUIRE(later);
  const Trail joined = fetched->trail + later->trail;
  CHECK(joined.stats == Stats{.exchanges = 3, .retries = 0, .bytes = 0});
}
