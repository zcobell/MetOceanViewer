// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <expected>
#include <string>
#include <string_view>
#include <variant>

#include "mov/io/detail/model_number.hpp"
#include "mov/io/detail/text.hpp"

namespace {

namespace detail = mov::io::detail;
using detail::ModelNumber;
using detail::NonFinite;
using detail::NumberError;
using detail::parse_model_number;

// The value of a token that must be a finite number.
double finite(std::string_view token) {
  const auto parsed = parse_model_number(token);
  REQUIRE(parsed.has_value());
  const double* value = std::get_if<double>(&*parsed);
  REQUIRE(value != nullptr);
  return *value;
}

bool non_finite(std::string_view token) {
  const auto parsed = parse_model_number(token);
  return parsed.has_value() and std::holds_alternative<NonFinite>(*parsed);
}

NumberError error_of(std::string_view token) {
  const auto parsed = parse_model_number(token);
  REQUIRE(not(parsed.has_value()));
  return parsed.error();
}

}  // namespace

TEST_CASE("parse_model_number reads what parse_double reads",
          "[io][detail][model_number]") {
  CHECK(finite("1.5") == 1.5);
  CHECK(finite("-9.9999000000E+004") == -99999.0);
  CHECK(finite("+1.0E+000") == 1.0);
  CHECK(finite("0.6000000E+003") == 600.0);
  CHECK(finite(".5") == 0.5);
  CHECK(finite("5.") == 5.0);
  CHECK(finite("1e3") == 1000.0);
  CHECK(std::signbit(finite("-0.0")));
}

TEST_CASE("parse_model_number maps Fortran's non-numbers to NonFinite",
          "[io][detail][model_number]") {
  for (const std::string_view token :
       {"NaN", "nan", "NAN", "+NaN", "-nan", "Inf", "inf", "+Inf", "-Inf",
        "Infinity", "-Infinity", "+INFINITY", "infinity", "*", "****",
        "********************"}) {
    INFO(token);
    CHECK(non_finite(token));
    CHECK(detail::is_nonfinite_token(token));
  }
}

TEST_CASE("parse_model_number rejects near misses of the non-numbers",
          "[io][detail][model_number]") {
  for (const std::string_view token :
       {"na", "nan1", "infin", "infinitx", "infinityy", "in", "*1", "1*",
        "**.*", "-*", "+", "-", "NaN%", "n an", "--inf", "i nf"}) {
    INFO(token);
    CHECK(not(non_finite(token)));
    CHECK(not(detail::is_nonfinite_token(token)));
    CHECK(error_of(token) == NumberError::bad_syntax);
  }
  CHECK(error_of("") == NumberError::empty);
  CHECK(not(detail::is_nonfinite_token("")));
}

TEST_CASE("parse_model_number: a magnitude no double holds is NonFinite",
          "[io][detail][model_number]") {
  CHECK(non_finite("1e400"));
  CHECK(non_finite("-1e400"));
  CHECK(non_finite("1e-400"));
  // A denormal is a number.
  CHECK(finite("5e-324") > 0.0);
}

TEST_CASE("parse_model_number reads a Fortran three-digit exponent",
          "[io][detail][model_number]") {
  CHECK(finite("1.2500000000-100") == 1.25e-100);
  CHECK(finite("-1.2500000000-100") == -1.25e-100);
  CHECK(finite("1.25+100") == 1.25e100);
  CHECK(finite("+1.25+100") == 1.25e100);
  CHECK(finite("1-5") == 1e-5);
  CHECK(finite("-1.0-308") == -1.0e-308);
  // Too small for a double: NonFinite, like any other underflow.
  CHECK(non_finite("1.0-999"));
  CHECK(non_finite("1.0+999"));
}

TEST_CASE("parse_model_number reads D exponents",
          "[io][detail][model_number]") {
  CHECK(finite("1.5D+02") == 150.0);
  CHECK(finite("1.5d+02") == 150.0);
  CHECK(finite("1.5D-02") == 0.015);
  CHECK(finite("-2D3") == -2000.0);
}

TEST_CASE("parse_model_number leaves other malformed tokens alone",
          "[io][detail][model_number]") {
  for (const std::string_view token :
       {"1.5+", "1.5-", "1.5D", "1.5D+", "1.5E+2+3", "1-2-3", "1.5e", "--1",
        "+-1", "0x10", "1,5", "1.5x", "1 5", ".", "D5", "E5", "1.5+D2"}) {
    INFO(token);
    CHECK(error_of(token) == NumberError::bad_syntax);
  }
}

TEST_CASE("parse_model_number does not rewrite a token with an E",
          "[io][detail][model_number]") {
  CHECK(finite("1.5E-100") == 1.5e-100);
  CHECK(error_of("1.5E-1-1") == NumberError::bad_syntax);
}

TEST_CASE("parse_model_number does not rewrite long tokens",
          "[io][detail][model_number]") {
  // The Fortran forms are rewritten in a small buffer; a token that does not
  // fit it is judged by parse_double's grammar alone.
  const std::string long_fortran = std::string(80, '1') + "-100";
  CHECK(error_of(long_fortran) == NumberError::bad_syntax);
  const std::string long_plain = "0." + std::string(80, '1');
  CHECK(finite(long_plain) > 0.1);
}

TEST_CASE("parse_model_number agrees with parse_double on every valid token",
          "[io][detail][model_number]") {
  for (const std::string_view token :
       {"0", "-0", "123456789", "1.", ".1", "1e5", "1E-5", "-1.5e+10",
        "9.9999000000E+004", "1.2345678901234567E-300"}) {
    INFO(token);
    const auto reference = detail::parse_double(token);
    REQUIRE(reference.has_value());
    CHECK(finite(token) == *reference);
  }
}
