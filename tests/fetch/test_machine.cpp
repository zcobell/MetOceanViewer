// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The fetch machine over scripted events (docs/providers-design.md §5.5,
// §5.6), with the toy spec of toy_spec.hpp.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "machine_script.hpp"
#include "mov/fetch/machine.hpp"
#include "toy_spec.hpp"

namespace {

using namespace mov::fetch;
using mov::fetch::test::Script;
using mov::fetch::test::t0;
using mov::fetch::test::ToyPage;
using mov::fetch::test::ToyRequest;
using mov::fetch::test::ToySpec;
using std::chrono::milliseconds;
using std::chrono::seconds;
using Values = std::vector<std::int64_t>;

ToyPage page_of(const Send<ToySpec::Target>& s) {
  return ToyPage{.values = {mov::fetch::test::encode(s.target)}};
}

/// Answers every send of the last batch with 200 and parses it.
template <class Sc>
void accept_all(Sc& sc, const std::vector<Send<ToySpec::Target>>& sends) {
  for (const auto& s : sends) {
    sc.respond(s.id, 200);
    REQUIRE(sc.parses().size() == 1);
    sc.parsed(s.id, page_of(s));
  }
}

Values concat(const std::vector<std::vector<std::int64_t>>& parts) {
  Values out;
  for (const auto& p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}

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
  // Completions and parses in the order 2, 0, 1.
  for (const std::size_t i : {std::size_t{2}, std::size_t{0}}) {
    sc.respond(sends[i].id, 200, 10);
    REQUIRE(sc.parses().size() == 1);
    CHECK(sc.parses()[0].id == sends[i].id);
  }
  sc.respond(sends[1].id, 200, 10);
  sc.parsed(sends[2].id, page_of(sends[2]));
  CHECK(sc.all<Progress>() == std::vector<Progress>{{.done = 1, .total = 3}});
  sc.parsed(sends[0].id, page_of(sends[0]),
            {mov::io::Warning{.code = mov::io::WarningCode::times_reordered}});
  sc.parsed(sends[1].id, page_of(sends[1]));
  const auto merge = sc.merge();

  const Values merged = concat(merge.in_plan_order);
  CHECK(std::ranges::is_sorted(merged));
  CHECK(merged.size() == 3);

  sc.merged(merged,
            {mov::io::Warning{.code = mov::io::WarningCode::epoch_used}});
  const auto finish = sc.finish();

  REQUIRE(finish.result);
  CHECK(finish.result->value == merged);
  CHECK(finish.result->stats ==
        Stats{.exchanges = 3, .retries = 0, .bytes = 30});
  // Unit warnings in plan order, then the merge's.
  REQUIRE(finish.result->warnings.size() == 2);
  CHECK(std::get<mov::io::Warning>(finish.result->warnings[0]).code ==
        mov::io::WarningCode::times_reordered);
  CHECK(std::get<mov::io::Warning>(finish.result->warnings[1]).code ==
        mov::io::WarningCode::epoch_used);
  CHECK(sc.machine().finished());
  // Nothing after Finish.
  CHECK(sc.cancel().empty());
  CHECK(sc.merged(merged).empty());
}

TEST_CASE("events naming unknown or settled ids cause no steps",
          "[fetch][machine]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto send = sc.sends().at(0);
  CHECK(sc.respond(ExchangeId{.value = 999}, 200).empty());
  CHECK(sc.elapse(TimerId{.value = 999}).empty());
  CHECK(sc.parsed(send.id, ToyPage{}).empty());  // not completed yet
  CHECK(sc.merged(Values{}).empty());            // no Merge step yet
  sc.respond(send.id, 200);
  CHECK(sc.respond(send.id, 200).empty());  // completed twice
}

TEST_CASE("a retried exchange gets a fresh id and the stale one is ignored",
          "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto first = sc.sends().at(0);
  sc.set_jitter(Jitter::from_bits(std::uint64_t{1} << 63U));
  sc.respond(first.id, 503);
  const auto waits = sc.all<Wait>();
  REQUIRE(waits.size() == 1);
  CHECK(waits[0].delay == milliseconds{500});  // half of base * 2^0
  sc.advance(waits[0].delay);
  sc.elapse(waits[0].id);
  const auto second = sc.sends().at(0);
  CHECK(second.id != first.id);
  CHECK(second.target == first.target);
  CHECK(sc.respond(first.id, 200).empty());  // the old attempt's late answer
  sc.respond(second.id, 200);
  sc.parsed(second.id, page_of(second));
  sc.merged(concat(sc.merge().in_plan_order));
  const auto result = sc.finish().result;
  REQUIRE(result);
  CHECK(result->stats.exchanges == 2);
  CHECK(result->stats.retries == 1);
  REQUIRE(result->warnings.size() == 1);
  const auto& retried =
      std::get<Retried>(std::get<FetchWarning>(result->warnings[0]));
  CHECK(retried.attempts == 2);
  CHECK(retried.unit == UnitLabel{CatalogPart{.index = 0}});
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
    return sc.first<Send<ToySpec::Target>>().id;
  };
  const ExchangeId last =
      fail_and_retry(fail_and_retry(fail_and_retry(sends[0].id)));
  sc.respond(last, 500);
  const auto aborts = sc.all<Abort>();
  REQUIRE(aborts.size() == 1);
  CHECK(aborts[0].ids == std::vector<ExchangeId>{other});
  const auto finish = sc.finish();

  REQUIRE_FALSE(finish.result);
  CHECK(finish.result.error() ==
        Failure{.error = HttpError{.status = 500, .origin = Origin::coops_data},
                .unit = CatalogPart{.index = 0}});
  CHECK(sc.respond(other, 200).empty());
}

TEST_CASE("transport failures retry or fail by their classification",
          "[fetch][machine][retry]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto id = sc.sends().at(0).id;
  sc.complete(id, TransportErrc::inactivity_timeout);
  const auto wait = sc.all<Wait>().at(0);
  sc.elapse(wait.id);
  sc.complete(sc.sends().at(0).id, TransportErrc::tls);
  const auto finish = sc.finish();

  CHECK(finish.result.error().error ==
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
  const auto retry = sc.all<Wait>().at(0);
  CHECK(retry.delay == milliseconds{5000});
  // The other unit fails transiently 1 s later; its retry may not go out
  // before the cool-down ends.
  sc.advance(seconds{1});
  sc.respond(sends[1].id, 503);
  const auto backoff_wait = sc.all<Wait>().at(0);
  CHECK(backoff_wait.delay == milliseconds{0});  // zero jitter
  sc.elapse(backoff_wait.id);
  CHECK(sc.sends().empty());
  const auto held = sc.all<Wait>().at(0);
  CHECK(held.delay == milliseconds{4000});
  sc.advance(held.delay);
  sc.elapse(held.id);
  REQUIRE(sc.sends().size() == 1);
  sc.elapse(retry.id);
  REQUIRE(sc.sends().size() == 1);
  CHECK(sc.sends()[0].target == sends[0].target);
}

TEST_CASE("a Retry-After beyond the policy fails with RateLimited",
          "[fetch][machine][cool-down]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  sc.complete(sc.sends().at(0).id,
              Response{.status = 429, .retry_after = seconds{121}});
  CHECK_FALSE(sc.has<CoolDown>());
  CHECK(sc.finish().result.error().error ==
        Error{RateLimited{.origin = Origin::coops_data,
                          .retry_after = seconds{121}}});
}

TEST_CASE(
    "an accepted response with no requests left cools down without a retry",
    "[fetch][machine][cool-down]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto id = sc.sends().at(0).id;
  sc.set_jitter(Jitter::from_bits(std::uint64_t{1} << 62U));
  sc.complete(id, Response{.status = 200, .rate_remaining = 0});
  CHECK(sc.all<CoolDown>().size() == 1);
  CHECK(sc.all<CoolDown>()[0].for_ == milliseconds{250});
  CHECK(sc.parses().size() == 1);
  CHECK_FALSE(sc.has<Wait>());
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
  REQUIRE(result->warnings.size() == 1);
  CHECK(std::get<UnitFailed>(std::get<FetchWarning>(result->warnings[0])) ==
        UnitFailed{
            .unit = CatalogPart{.index = 1000},
            .error = HttpError{.status = 404, .origin = Origin::coops_data}});
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
                .unit = CatalogPart{.index = 0}});
}

TEST_CASE("a lenient unit that fails drops its other exchanges",
          "[fetch][machine][lenient]") {
  Script<ToySpec> sc{ToyRequest{.units = 2, .slots = 2},
                     Options{.strictness = Strictness::lenient}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 400);
  CHECK(sc.all<Abort>() == std::vector<Abort>{{.ids = {sends[1].id}}});
  CHECK(sc.respond(sends[1].id, 200).empty());
  accept_all(sc, {sends[2], sends[3]});
  CHECK(concat(sc.merge().in_plan_order).size() == 2);
}

TEST_CASE("no data anywhere is NoData, decided at the end",
          "[fetch][machine][no-data]") {
  Script<ToySpec> sc{ToyRequest{.units = 2, .origin = Origin::ndbc}, Options{}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 404);  // NDBC: absent
  CHECK_FALSE(sc.finished_now());
  sc.respond(sends[1].id, 200);
  sc.parse_failed(sends[1].id, NoData{});
  CHECK(sc.finish().result.error() ==
        Failure{.error = NoData{}, .unit = std::nullopt});
}

TEST_CASE("empty parts are gaps, not data", "[fetch][machine][no-data]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  const auto id = sc.sends().at(0).id;
  sc.respond(id, 200);
  sc.parsed(id, ToyPage{});
  CHECK(sc.finish().result.error().error == Error{NoData{}});
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
        Failure{.error = error, .unit = CatalogPart{.index = 0}});
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
  const auto wait = sc.all<Wait>().at(0);
  sc.elapse(wait.id);
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

TEST_CASE("cancel aborts the exchanges in flight and finishes Cancelled",
          "[fetch][machine][cancel]") {
  Script<ToySpec> sc{ToyRequest{.units = 2}, Options{}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 200);  // parsing: not an exchange any more
  sc.cancel();
  CHECK(sc.all<Abort>() == std::vector<Abort>{{.ids = {sends[1].id}}});
  CHECK(sc.finish().result.error() ==
        Failure{.error = mov::core::Cancelled{}, .unit = std::nullopt});
  CHECK(sc.steps().size() == 2);
  CHECK(sc.parsed(sends[0].id, page_of(sends[0])).empty());
}

TEST_CASE("cancel during the merge finishes Cancelled",
          "[fetch][machine][cancel]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  accept_all(sc, sc.sends());
  CHECK(sc.has<Merge<Values>>());
  sc.cancel();
  CHECK(sc.finish().result.error().error == Error{mov::core::Cancelled{}});
  CHECK(sc.merged(Values{1}).empty());
}

TEST_CASE("a failed merge is the fetch's failure", "[fetch][machine]") {
  Script<ToySpec> sc{ToyRequest{.units = 1}, Options{}};
  accept_all(sc, sc.sends());
  sc.on(event::Merged<Values>{
      .value = std::unexpected{Error{MetaMismatch{.column = "x"}}}});
  CHECK(sc.finish().result.error() ==
        Failure{.error = MetaMismatch{.column = "x"}, .unit = std::nullopt});
}

TEST_CASE("the deadline aborts everything and finishes DeadlineExceeded",
          "[fetch][machine][deadline]") {
  Script<ToySpec> sc{ToyRequest{.units = 2},
                     Options{.deadline = t0 + seconds{10}}};
  const auto timer = sc.all<Wait>().at(0);
  CHECK(timer.delay == milliseconds{10000});
  const auto sends = sc.sends();
  // An early timer re-arms for the rest.
  sc.advance(seconds{4});
  sc.elapse(timer.id);
  const auto rearmed = sc.all<Wait>().at(0);
  CHECK(rearmed.delay == milliseconds{6000});
  CHECK_FALSE(sc.finished_now());
  sc.advance(seconds{6});
  sc.elapse(rearmed.id);
  REQUIRE(sc.all<Abort>().size() == 1);
  CHECK(sc.all<Abort>()[0].ids.size() == 2);
  CHECK(sc.finish().result.error().error == Error{DeadlineExceeded{}});
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

TEST_CASE("a failing plan finishes at start", "[fetch][machine]") {
  const Script<ToySpec> sc{ToyRequest{.fail_initial = true}, Options{}};
  CHECK(sc.finish().result.error().error == Error{NoData{}});
}

TEST_CASE("no units is NoData", "[fetch][machine][no-data]") {
  const Script<ToySpec> sc{ToyRequest{.units = 0}, Options{}};
  CHECK(sc.finish().result.error().error == Error{NoData{}});
}

TEST_CASE("a unit without exchanges settles at once",
          "[fetch][machine][chain]") {
  const Script<ToySpec> sc{ToyRequest{.units = 2, .slots = 0, .pages = 2},
                           Options{}};
  CHECK(sc.sends().empty());
  // Every unit received nothing: no data.
  CHECK(sc.finish().result.error().error == Error{NoData{}});
  CHECK(sc.all<Progress>().back() == Progress{.done = 4, .total = 4});
  // A strict failure in such a successor ends the stage at once.
  const Script<ToySpec> failing{
      ToyRequest{.units = 1, .slots = 0, .pages = 3, .fail_next = true},
      Options{}};
  CHECK(failing.finish().result.error() ==
        Failure{.error = MetaMismatch{.column = "toy"},
                .unit = CatalogPart{.index = 1}});
  CHECK(std::holds_alternative<Finish<Values>>(failing.steps().back()));
}

TEST_CASE("next adds units: pages follow their unit and the total grows",
          "[fetch][machine][chain]") {
  Script<ToySpec> sc{ToyRequest{.units = 2, .pages = 3}, Options{}};
  const auto sends = sc.sends();
  std::vector<Progress> progress;
  std::vector<Send<ToySpec::Target>> queue{sends.begin(), sends.end()};
  while (not queue.empty()) {
    const auto s = queue.back();
    queue.pop_back();
    sc.respond(s.id, 200);
    sc.parsed(s.id, page_of(s));
    for (const auto& p : sc.all<Progress>()) {
      progress.push_back(p);
    }
    for (const auto& n : sc.sends()) {
      queue.push_back(n);
    }
  }
  REQUIRE(progress.size() == 6);
  CHECK(progress.back() == Progress{.done = 6, .total = 6});
  CHECK(std::ranges::is_sorted(progress, {}, &Progress::done));
  const Values merged = concat(sc.merge().in_plan_order);
  CHECK(merged.size() == 6);
  CHECK(std::ranges::is_sorted(merged));
}

TEST_CASE("an absent unit falls back; an absent slot discards its sibling",
          "[fetch][machine][chain]") {
  Script<ToySpec> sc{
      ToyRequest{
          .units = 1, .slots = 2, .fallback = true, .origin = Origin::ndbc},
      Options{}};
  const auto sends = sc.sends();
  sc.respond(sends[0].id, 200);
  sc.parsed(sends[0].id, page_of(sends[0]));
  sc.respond(sends[1].id, 404);
  const auto fallback = sc.sends();
  REQUIRE(fallback.size() == 2);
  CHECK(fallback[0].target.unit.fallback);
  accept_all(sc, fallback);
  const Values merged = concat(sc.merge().in_plan_order);
  CHECK(merged == Values{mov::fetch::test::encode(fallback[0].target),
                         mov::fetch::test::encode(fallback[1].target)});
}

TEST_CASE("a next that fails fails its unit", "[fetch][machine][chain]") {
  Script<ToySpec> sc{ToyRequest{.units = 1, .pages = 3, .fail_next = true},
                     Options{}};
  auto s = sc.sends().at(0);
  sc.respond(s.id, 200);
  sc.parsed(s.id, page_of(s));
  s = sc.sends().at(0);
  sc.respond(s.id, 200);
  sc.parsed(s.id, page_of(s));
  CHECK(sc.finish().result.error() ==
        Failure{.error = MetaMismatch{.column = "toy"},
                .unit = CatalogPart{.index = 1}});
}

TEST_CASE("a second stage continues the first's ids and trail",
          "[fetch][machine][stages]") {
  Script<ToySpec> first{ToyRequest{.units = 1}, Options{}};
  accept_all(first, first.sends());
  first.merged(Values{7});
  auto [value, trail] = split(std::move(*first.finish().result));
  CHECK(value == Values{7});

  Script<ToySpec> second{
      ToyRequest{.units = static_cast<std::uint32_t>(value.front()) - 5},
      Options{.priority = Priority::background,
              .first_id = first.machine().next_id()}};
  const auto sends = second.sends();
  REQUIRE(sends.size() == 2);
  CHECK(sends[0].id.value >= first.machine().next_id());
  CHECK(sends[0].priority == Priority::background);
  accept_all(second, sends);
  second.merged(Values{1, 2});
  const auto joined =
      after(std::move(trail), std::move(*second.finish().result));
  CHECK(joined.value == Values{1, 2});
  CHECK(joined.stats == Stats{.exchanges = 3, .retries = 0, .bytes = 0});
}
