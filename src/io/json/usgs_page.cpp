// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The envelope and features of a USGS page (usgs_page.hpp).

#include "usgs_page.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/io/detail/json.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/usgs.hpp"

namespace mov::io::wire::usgs::page {

namespace {

// The value of the query parameter `name` of `url` ("" for a bare name),
// or nullopt when it has none.
std::optional<std::string_view> parameter(std::string_view url,
                                          std::string_view name) {
  const std::size_t mark = url.find('?');
  if (mark == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view query = url.substr(mark + 1);
  while (not query.empty()) {
    const std::size_t amp = query.find('&');
    const std::string_view pair = query.substr(0, amp);
    const std::size_t equals = pair.find('=');
    if (pair.substr(0, equals) == name) {
      return equals == std::string_view::npos ? std::string_view{}
                                              : pair.substr(equals + 1);
    }
    if (amp == std::string_view::npos) {
      break;
    }
    query.remove_prefix(amp + 1);
  }
  return std::nullopt;
}

// Whether an offset link (no cursor) is the API's echo after a full last
// page: its `offset` and its `limit` both the page's number of features.
bool is_offset_echo(std::string_view url, std::string_view offset,
                    std::size_t features) {
  const auto limit = parameter(url, "limit");
  if (not limit) {
    return false;
  }
  const auto at = io::detail::parse_int<std::uint64_t>(offset);
  const auto rows = io::detail::parse_int<std::uint64_t>(*limit);
  return at and rows and *at == features and *rows == features;
}

// The `next` link of `links`, if any: at most one.
std::expected<const json::Value*, Error> next_href(std::string_view body,
                                                   const json::Value& links) {
  const json::Array* list = json::as_array(links);
  if (list == nullptr) {
    return fail_at(body, ParseErrc::bad_member, links.offset);
  }
  const json::Value* found = nullptr;
  for (const json::Value& link : *list) {
    if (json::as_object(link) == nullptr) {
      return fail_at(body, ParseErrc::bad_member, link.offset);
    }
    const json::Value* rel = json::member(link, "rel");
    const std::string* rel_text =
        rel == nullptr ? nullptr : json::as_string(*rel);
    if (rel_text == nullptr or *rel_text != "next") {
      continue;
    }
    if (found != nullptr) {
      return fail_at(body, ParseErrc::bad_member, link.offset);
    }
    const json::Value* href = json::member(link, "href");
    if (href == nullptr or json::as_string(*href) == nullptr) {
      return fail_at(body, ParseErrc::bad_member,
                     href == nullptr ? link.offset : href->offset);
    }
    found = href;
  }
  return found;
}

// The next page a link names, after Verbatim's check.
std::expected<NextPage, Error> next_page(std::string_view url,
                                         std::size_t features,
                                         Endpoint endpoint) {
  auto link = Verbatim::make(Origin::usgs, items_path(endpoint), url);
  if (not link) {
    return io::detail::fail(io::detail::format_error(
        FormatErrc::refused_link,
        io::detail::subject_of(std::string{to_token(link.error())} + " " +
                               std::string{url})));
  }
  const auto offset = parameter(url, "offset");
  if (not offset or parameter(url, "cursor")) {
    return NextPage{Next{.link = *std::move(link)}};
  }
  if (not is_offset_echo(url, *offset, features)) {
    return io::detail::fail(io::detail::format_error(
        FormatErrc::unexpected_link, io::detail::subject_of(url)));
  }
  return NextPage{OffsetEcho{.link = *std::move(link)}};
}

// The string member `name` of `object`: nullptr when it is absent or null,
// bad_member at it when it is another JSON type.
std::expected<const std::string*, Error> optional_string(
    std::string_view body, const json::Value& object, std::string_view name) {
  const json::Value* value = json::member(object, name);
  if (value == nullptr or json::is_null(*value)) {
    return nullptr;
  }
  const std::string* text = json::as_string(*value);
  if (text == nullptr) {
    return fail_at(body, ParseErrc::bad_member, value->offset);
  }
  return text;
}

}  // namespace

std::unexpected<Error> fail_at(std::string_view body, ParseErrc code,
                               std::size_t offset) {
  return io::detail::fail(json::error_at(code, body, offset));
}

std::expected<Paging, Error> read_paging(std::string_view body,
                                         const json::Value& root,
                                         std::size_t features,
                                         Endpoint endpoint) {
  const json::Value* array = json::member(root, "features");
  if (array == nullptr or json::as_array(*array) == nullptr) {
    return fail_at(body, ParseErrc::bad_member,
                   array == nullptr ? root.offset : array->offset);
  }
  if (const json::Value* returned = json::member(root, "numberReturned")) {
    const std::optional<std::int64_t> count = json::as_int(*returned);
    if (not count or *count < 0) {
      return fail_at(body, ParseErrc::bad_member, returned->offset);
    }
    if (std::cmp_not_equal(*count, features)) {
      return fail_at(body, ParseErrc::count_mismatch, returned->offset);
    }
  }
  Paging paging{.features = features, .next = NoNext{}};
  const json::Value* links = json::member(root, "links");
  if (links == nullptr) {
    return paging;
  }
  const auto href = next_href(body, *links);
  if (not href) {
    return std::unexpected{href.error()};
  }
  if (*href == nullptr) {
    return paging;
  }
  auto next = next_page(*json::as_string(**href), features, endpoint);
  if (not next) {
    return std::unexpected{std::move(next).error()};
  }
  paging.next = *std::move(next);
  return paging;
}

std::expected<void, Error> check_room(std::string_view body, std::size_t index,
                                      const json::Value& feature,
                                      const ReadContext& ctx) {
  if (index + 1 > ctx.limits.max_elements) {
    return io::detail::fail(ParseError::make(
        ParseErrc::too_large, io::detail::where_of(body, feature.offset),
        std::to_string(index + 1) + " elements, limit " +
            std::to_string(ctx.limits.max_elements)));
  }
  return {};
}

std::expected<const json::Value*, Error> properties_of(
    std::string_view body, const json::Value& feature) {
  if (json::as_object(feature) == nullptr) {
    return fail_at(body, ParseErrc::bad_member, feature.offset);
  }
  const json::Value* properties = json::member(feature, "properties");
  if (properties == nullptr) {
    return fail_at(body, ParseErrc::bad_member, feature.offset);
  }
  if (json::as_object(*properties) == nullptr) {
    return fail_at(body, ParseErrc::bad_member, properties->offset);
  }
  return properties;
}

// ---- counts
// ------------------------------------------------------------------------

void TextCounts::add(std::string_view text, std::size_t count) {
  const std::string subject = io::detail::subject_of(text);
  if (const auto it = counts_.find(subject); it != counts_.end()) {
    it->second += count;
  } else if (counts_.size() < max_subjects) {
    counts_.emplace(subject, count);
  } else {
    others_ += count;
  }
}

void TextCounts::report(std::vector<Warning>& warnings,
                        WarningCode code) const {
  for (const auto& [subject, count] : counts_) {
    warnings.push_back({.code = code, .subject = subject, .count = count});
  }
  append_if_counted(warnings,
                    {.code = code, .subject = "...", .count = others_});
}

std::expected<void, Error> Marks::add(std::string_view body,
                                      const json::Value& properties) {
  const auto approval = optional_string(body, properties, "approval_status");
  if (not approval) {
    return std::unexpected{approval.error()};
  }
  if (*approval != nullptr and **approval == "Provisional") {
    ++provisional_;
  }
  const json::Value* qualifier = json::member(properties, "qualifier");
  if (qualifier == nullptr or json::is_null(*qualifier)) {
    return {};
  }
  if (const std::string* text = json::as_string(*qualifier)) {
    qualifiers_.add(*text);
    return {};
  }
  const json::Array* list = json::as_array(*qualifier);
  if (list == nullptr) {
    return fail_at(body, ParseErrc::bad_member, qualifier->offset);
  }
  for (const json::Value& entry : *list) {
    const std::string* text = json::as_string(entry);
    if (text == nullptr) {
      return fail_at(body, ParseErrc::bad_member, entry.offset);
    }
    qualifiers_.add(*text);
  }
  return {};
}

void Marks::report(std::vector<Warning>& warnings) const {
  append_if_counted(warnings, {.code = WarningCode::provisional_samples,
                               .subject = "usgs:approval_status",
                               .count = provisional_});
  qualifiers_.report(warnings, WarningCode::qualified_samples);
}

}  // namespace mov::io::wire::usgs::page
