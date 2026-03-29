#include "decimals.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>

#include <criterion/criterion.h>

using namespace decimals;

// Tests that verify decimal behaves like double should also assert the expected
// double behavior directly, so that if the C library's behavior changes or
// differs across platforms the test fails explicitly rather than silently
// testing the wrong thing.

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool same_double(double a, double b) {
  if (std::isnan(a) && std::isnan(b)) return true;
  if (std::isnan(a) || std::isnan(b)) return false;
  uint64_t ai, bi;
  std::memcpy(&ai, &a, sizeof(a));
  std::memcpy(&bi, &b, sizeof(b));
  return ai == bi;
}

static constexpr double dnan = std::numeric_limits<double>::quiet_NaN();
static constexpr double dinf = std::numeric_limits<double>::infinity();

// Criterion's cr_assert_str_eq stores .c_str() in a local variable, so the
// temporary std::string from .to_string() is dead before strcmp runs.  This
// macro keeps the string alive through the comparison.
#define ASSERT_TOSTR_EQ(expr, expected) do {           \
  std::string _ts = (expr).to_string();                \
  cr_assert_str_eq(_ts.c_str(), expected);             \
} while (0)

// ---------------------------------------------------------------------------
// Construction & queries
// ---------------------------------------------------------------------------

Test(construction, nan_value) {
  decimal d = decimal::nan();
  cr_assert(d.is_nan());
  cr_assert(!d.is_inf());
  cr_assert(!d.is_zero());
  cr_assert(!d.is_finite());

  // Verify our assumptions about double NaN.
  cr_assert(std::isnan(dnan));
  cr_assert(!std::isinf(dnan));
  cr_assert(!std::isfinite(dnan));
}

Test(construction, inf_positive) {
  decimal d = decimal::inf();
  cr_assert(d.is_inf());
  cr_assert(!d.is_nan());
  cr_assert(!d.is_zero());
  cr_assert(!d.is_finite());
  cr_assert(!d.is_negative());

  cr_assert(std::isinf(dinf));
  cr_assert(!std::isnan(dinf));
  cr_assert(!std::isfinite(dinf));
  cr_assert(dinf > 0);
}

Test(construction, inf_negative) {
  decimal d = decimal::inf(NEGATIVE);
  cr_assert(d.is_inf());
  cr_assert(d.is_negative());

  cr_assert(std::isinf(-dinf));
  cr_assert(-dinf < 0);
}

Test(construction, zero_positive) {
  decimal d = decimal::zero();
  cr_assert(d.is_zero());
  cr_assert(!d.is_negative());
}

Test(construction, zero_negative) {
  decimal d = decimal::zero(NEGATIVE);
  cr_assert(d.is_zero());
  cr_assert(d.is_negative());

  cr_assert(std::signbit(-0.0));
  cr_assert(-0.0 == 0.0);
}

Test(construction, from_double_positive) {
  decimal d(1.5);
  cr_assert(!d.is_zero());
  cr_assert(!d.is_nan());
  cr_assert(!d.is_inf());
  ASSERT_TOSTR_EQ(d, "1.5");
}

Test(construction, from_double_negative) {
  decimal d(-42.0);
  cr_assert(d.is_negative());
  ASSERT_TOSTR_EQ(d, "-42");
}

Test(construction, from_int64_positive) {
  decimal d(static_cast<int64_t>(42));
  ASSERT_TOSTR_EQ(d, "42");
  cr_assert(!d.is_negative());
}

Test(construction, from_int64_negative) {
  decimal d(static_cast<int64_t>(-42));
  ASSERT_TOSTR_EQ(d, "-42");
  cr_assert(d.is_negative());
}

Test(construction, from_int64_zero) {
  decimal d(static_cast<int64_t>(0));
  cr_assert(d.is_zero());
  cr_assert(!d.is_negative());
}

Test(construction, from_int64_min) {
  decimal d(INT64_MIN);
  cr_assert(d.is_negative());
  cr_assert(!d.is_zero());
  // INT64_MIN = -9223372036854775808, which has 19 digits → exact.
  cr_assert_eq(d.to_double(), static_cast<double>(INT64_MIN));
}

Test(construction, from_int64_max) {
  decimal d(INT64_MAX);
  cr_assert(!d.is_negative());
  // INT64_MAX = 9223372036854775807, which has 19 digits → exact.
  std::string s = d.to_string();
  cr_assert_str_eq(s.c_str(), "9223372036854775807");
}

Test(construction, from_uint64_large) {
  decimal d(UINT64_MAX);
  cr_assert(!d.is_negative());
  cr_assert(!d.is_zero());
  // UINT64_MAX = 18446744073709551615 (20 digits — rounded to 19).
  cr_assert(d.mantissa() >= 1000000000000000000ULL);
  cr_assert(d.mantissa() < 10000000000000000000ULL);
}

Test(construction, from_uint64_zero) {
  decimal d(static_cast<uint64_t>(0));
  cr_assert(d.is_zero());
}

Test(construction, from_uint64_exact) {
  decimal d(static_cast<uint64_t>(1234567890123456789ULL));
  std::string s = d.to_string();
  cr_assert_str_eq(s.c_str(), "1234567890123456789");
}

Test(construction, from_int_roundtrip) {
  // All int32 values are representable exactly.
  int32_t vals[] = {0, 1, -1, 42, -42, INT32_MAX, INT32_MIN};
  for (int32_t v : vals) {
    decimal d(static_cast<int64_t>(v));
    double back = d.to_double();
    cr_assert_eq(back, static_cast<double>(v),
                 "int roundtrip failed for %d", v);
  }
}

Test(construction, int64_roundtrip) {
  // All int64 values are representable exactly (at most 19 digits).
  int64_t vals[] = {0, 1, -1, INT64_MAX, INT64_MIN,
                    INT64_MAX - 1, INT64_MIN + 1};
  for (int64_t v : vals) {
    decimal d(v);
    int64_t back = static_cast<int64_t>(d);
    cr_assert_eq(back, v, "int64 roundtrip failed for %lld", (long long)v);
  }
}

Test(construction, uint64_roundtrip) {
  // uint64 values up to max_exact_uint64 are representable exactly.
  uint64_t vals[] = {0, 1, decimal::max_exact_uint64,
                     decimal::max_exact_uint64 - 1,
                     static_cast<uint64_t>(INT64_MAX) + 1};
  for (uint64_t v : vals) {
    decimal d(v);
    uint64_t back = static_cast<uint64_t>(d);
    cr_assert_eq(back, v, "uint64 roundtrip failed for %llu", (unsigned long long)v);
  }
}

Test(construction, from_double_nan) {
  decimal d(dnan);
  cr_assert(d.is_nan());
}

Test(construction, from_double_inf) {
  decimal d(dinf);
  cr_assert(d.is_inf());
  cr_assert(!d.is_negative());
}

Test(construction, from_double_neg_inf) {
  decimal d(-dinf);
  cr_assert(d.is_inf());
  cr_assert(d.is_negative());
}

Test(construction, from_double_neg_zero) {
  decimal d(-0.0);
  cr_assert(d.is_zero());
  cr_assert(d.is_negative());
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

Test(accessors, exponent_mantissa) {
  // 1.23 = 1230000000000000000 * 10^-18
  decimal d = decimal::from_string("1.23");
  cr_assert(!d.is_negative());
  cr_assert_eq(d.mantissa(), 1230000000000000000ULL);
  cr_assert_eq(d.exponent(), -18);
}

Test(accessors, negative_sign) {
  // -456 = 4560000000000000000 * 10^-16
  decimal d = decimal::from_string("-456");
  cr_assert(d.is_negative());
  cr_assert_eq(d.mantissa(), 4560000000000000000ULL);
  cr_assert_eq(d.exponent(), -16);
}

Test(accessors, large_exponent) {
  // 5e3000 = 5000000000000000000 * 10^2982
  decimal d = decimal::from_string("5e3000");
  cr_assert_eq(d.mantissa(), 5000000000000000000ULL);
  cr_assert_eq(d.exponent(), 2982);
}

Test(accessors, small_exponent) {
  // 7e-3000 = 7000000000000000000 * 10^-3018
  decimal d = decimal::from_string("7e-3000");
  cr_assert_eq(d.mantissa(), 7000000000000000000ULL);
  cr_assert_eq(d.exponent(), -3018);
}

Test(accessors, mantissa_always_19_digits) {
  const char* cases[] = {"1", "0.1", "99", "12345678901234567", "1e100",
                         "0.000001"};
  for (auto s : cases) {
    decimal d = decimal::from_string(s);
    cr_assert(d.mantissa() >= 1000000000000000000ULL,
              "\"%s\": mantissa %llu < 10^18", s,
              (unsigned long long)d.mantissa());
    cr_assert(d.mantissa() < 10000000000000000000ULL,
              "\"%s\": mantissa %llu >= 10^19", s,
              (unsigned long long)d.mantissa());
  }
}

// ---------------------------------------------------------------------------
// from_string
// ---------------------------------------------------------------------------

Test(from_string, integer) {
  decimal d = decimal::from_string("12345");
  ASSERT_TOSTR_EQ(d, "12345");
}

Test(from_string, decimal) {
  decimal d = decimal::from_string("123.456");
  ASSERT_TOSTR_EQ(d, "123.456");
}

Test(from_string, leading_zeros) {
  decimal d = decimal::from_string("007.5");
  ASSERT_TOSTR_EQ(d, "7.5");
}

Test(from_string, leading_dot) {
  decimal d = decimal::from_string(".25");
  ASSERT_TOSTR_EQ(d, "0.25");
}

Test(from_string, trailing_dot) {
  decimal d = decimal::from_string("42.");
  ASSERT_TOSTR_EQ(d, "42");
}

Test(from_string, zero_point_something) {
  decimal d = decimal::from_string("0.001");
  ASSERT_TOSTR_EQ(d, "0.001");
}

Test(from_string, scientific_positive_exp) {
  decimal d = decimal::from_string("2.34e56");
  cr_assert_eq(d.mantissa(), 2340000000000000000ULL);
  cr_assert_eq(d.exponent(), 38);
}

Test(from_string, scientific_negative_exp) {
  decimal d = decimal::from_string("5.6e-3");
  ASSERT_TOSTR_EQ(d, "0.0056");
}

Test(from_string, scientific_uppercase) {
  decimal d = decimal::from_string("1E10");
  cr_assert_eq(d.mantissa(), 1000000000000000000ULL);
  cr_assert_eq(d.exponent(), -8);
}

Test(from_string, scientific_positive_sign) {
  decimal d = decimal::from_string("3.14e+2");
  ASSERT_TOSTR_EQ(d, "314");
}

Test(from_string, nan_lower) {
  decimal d = decimal::from_string("nan");
  cr_assert(d.is_nan());
}

Test(from_string, nan_upper) {
  decimal d = decimal::from_string("NAN");
  cr_assert(d.is_nan());
}

Test(from_string, nan_mixed) {
  decimal d = decimal::from_string("NaN");
  cr_assert(d.is_nan());
}

Test(from_string, inf_lower) {
  decimal d = decimal::from_string("inf");
  cr_assert(d.is_inf());
  cr_assert(!d.is_negative());
}

Test(from_string, inf_negative) {
  decimal d = decimal::from_string("-inf");
  cr_assert(d.is_inf());
  cr_assert(d.is_negative());
}

Test(from_string, infinity_full) {
  decimal d = decimal::from_string("infinity");
  cr_assert(d.is_inf());
}

Test(from_string, infinity_upper) {
  decimal d = decimal::from_string("INFINITY");
  cr_assert(d.is_inf());
}

Test(from_string, leading_whitespace) {
  decimal d = decimal::from_string("  \t 42");
  ASSERT_TOSTR_EQ(d, "42");
}

Test(from_string, positive_sign) {
  decimal d = decimal::from_string("+99");
  ASSERT_TOSTR_EQ(d, "99");
  cr_assert(!d.is_negative());
}

Test(from_string, empty_string) {
  const char* end = nullptr;
  decimal d = decimal::from_string("", &end);
  cr_assert(d.is_zero());
}

Test(from_string, no_digits) {
  const char* end = nullptr;
  decimal d = decimal::from_string("abc", &end);
  cr_assert(d.is_zero());
  cr_assert_str_eq(end, "abc");
}

Test(from_string, trailing_garbage) {
  const char* end = nullptr;
  decimal d = decimal::from_string("123abc", &end);
  ASSERT_TOSTR_EQ(d, "123");
  cr_assert_str_eq(end, "abc");
}

Test(from_string, e_no_digits_after) {
  const char* end = nullptr;
  decimal d = decimal::from_string("5e", &end);
  ASSERT_TOSTR_EQ(d, "5");
  cr_assert_str_eq(end, "e");
}

Test(from_string, e_no_digits_after_sign) {
  const char* end = nullptr;
  decimal d = decimal::from_string("5e+", &end);
  ASSERT_TOSTR_EQ(d, "5");
  cr_assert_str_eq(end, "e+");
}

Test(from_string, just_zero) {
  decimal d = decimal::from_string("0");
  cr_assert(d.is_zero());
  ASSERT_TOSTR_EQ(d, "0");
}

Test(from_string, negative_zero) {
  decimal d = decimal::from_string("-0");
  cr_assert(d.is_zero());
  cr_assert(d.is_negative());
}

Test(from_string, multiple_leading_zeros) {
  decimal d = decimal::from_string("0000.0001");
  ASSERT_TOSTR_EQ(d, "0.0001");
}

Test(from_string, huge_exponent_overflow) {
  decimal d = decimal::from_string("1e9999999999999999999");
  cr_assert(d.is_inf());
}

Test(from_string, huge_negative_exponent_underflow) {
  decimal d = decimal::from_string("1e-9999999999999999999");
  cr_assert(d.is_zero());
}

Test(from_string, many_significant_digits) {
  decimal d = decimal::from_string("12345678901234567890.5");
  cr_assert(!d.is_zero());
  cr_assert(!d.is_nan());
  cr_assert(!d.is_inf());
}

// Dot-transition while dropping: 20+ digits before the dot, then fractional digits.
// The '0' at position 20 should be dropped (no round-up), and the '.5' should NOT
// be re-accumulated into the mantissa.  Expected: 12345678901234567890.
Test(from_string, dot_transition_preserves_dropping_state) {
  ASSERT_TOSTR_EQ(decimal::from_string("12345678901234567890.5"), "12345678901234567890");
}

// Same bug, different rounding: the 20th digit '5' should round up the mantissa,
// and the post-dot digits should be ignored (already dropping).
Test(from_string, dot_transition_dropping_with_round_up) {
  ASSERT_TOSTR_EQ(decimal::from_string("12345678901234567895.9"), "12345678901234567900");
}

// Extra digits both before and after the dot while dropping.
Test(from_string, dot_transition_dropping_many_extra) {
  std::string s = decimal::from_string("1234567890123456789012.345").to_string();
  cr_assert_str_eq(s.c_str(), "1.234567890123456789e21");
}

// Exactly 19 significant digits — no rounding, exact roundtrip.
Test(from_string, nineteen_sig_digits_integer) {
  ASSERT_TOSTR_EQ(decimal::from_string("1234567890123456789"), "1234567890123456789");
}

Test(from_string, nineteen_nines) {
  ASSERT_TOSTR_EQ(decimal::from_string("9999999999999999999"), "9999999999999999999");
}

Test(from_string, nineteen_sig_digits_fraction) {
  ASSERT_TOSTR_EQ(decimal::from_string("0.1234567890123456789"), "0.1234567890123456789");
}

// Regression: mant_digits was a local variable in parse_mantissa, so it reset
// to 0 when the recursive call crossed the decimal point.  With 10 digits
// before and 19 after the dot, 29 digits were accumulated into a uint64_t,
// overflowing the mantissa.
Test(from_string, mant_digits_spans_dot) {
  // 10 before dot + 19 after = 29 total significant digits.
  // Only the first 19 should be kept; the 20th digit ('0') rounds down.
  // Expected 19-digit mantissa: 1234567890123456789, with 10 digits after dot
  // → "1234567890.123456789"
  ASSERT_TOSTR_EQ(decimal::from_string("1234567890.1234567890000000000"), "1234567890.123456789");
  // Same idea: 5 before dot + 20 after.  20th significant digit is '5' → round up.
  ASSERT_TOSTR_EQ(decimal::from_string("12345.67890123456789050000"), "12345.67890123456789");
}

// 20 significant digits — the 20th digit drives rounding.
// first_dropped='0' → round down; the value is stored as 9999999999999999999e1.
Test(from_string, round_down_at_twentieth_digit) {
  ASSERT_TOSTR_EQ(decimal::from_string("99999999999999999990"), "99999999999999999990");
}

// first_dropped='9' → round up 9999999999999999999 → 10^19; make() renormalises
// to m=10^18, exp adjusted +1, giving 1e20 in scientific notation.
Test(from_string, round_up_at_twentieth_digit) {
  ASSERT_TOSTR_EQ(decimal::from_string("99999999999999999999"), "1e20");
}

// Negative values with decimal points — not covered in the from_string suite.
Test(from_string, negative_with_decimal) {
  ASSERT_TOSTR_EQ(decimal::from_string("-123.456"), "-123.456");
}

Test(from_string, negative_scientific_negative_exp) {
  ASSERT_TOSTR_EQ(decimal::from_string("-1.23e-5"), "-0.0000123");
}

// '+' sign prefix with a fractional value (only "+integer" was tested before).
Test(from_string, positive_sign_with_decimal) {
  ASSERT_TOSTR_EQ(decimal::from_string("+1.5"), "1.5");
}

// Roundtrip for values near the plain/scientific threshold (positive and negative).
Test(from_string, roundtrip_near_threshold) {
  const char* cases[] = {
    "1.23456789e19",   // dp=20, plain
    "1.23456789e20",   // dp=21, scientific
    "1.23456789e-22",  // dp<-20, scientific
    "-1.23456789e19",
    "-1.23456789e20",
    "-1.23456789e-22",
  };
  for (auto s : cases) {
    decimal d = decimal::from_string(s);
    std::string s1 = d.to_string();
    decimal d2 = decimal::from_string(s1.c_str());
    cr_assert(d == d2, "roundtrip failed for \"%s\" → \"%s\"", s, s1.c_str());
  }
}

Test(from_string, negative_nan) {
  decimal d = decimal::from_string("-nan");
  cr_assert(d.is_nan());
}

Test(from_string, hex_float_simple) {
  double dv = std::strtod("0x1p0", nullptr);
  cr_assert_eq(dv, 1.0);
  decimal d = decimal::from_string("0x1p0");
  cr_assert(same_double(d.to_double(), dv));
}

Test(from_string, hex_float_with_fraction) {
  double dv = std::strtod("0x1.8p0", nullptr);
  cr_assert_eq(dv, 1.5);
  decimal d = decimal::from_string("0x1.8p0");
  cr_assert(same_double(d.to_double(), dv));
}

Test(from_string, hex_float_with_exponent) {
  double dv = std::strtod("0x1.fp10", nullptr);
  cr_assert_eq(dv, 1984.0);
  decimal d = decimal::from_string("0x1.fp10");
  cr_assert(same_double(d.to_double(), dv));
}

Test(from_string, hex_float_negative) {
  double dv = std::strtod("-0x1p1", nullptr);
  cr_assert_eq(dv, -2.0);
  decimal d = decimal::from_string("-0x1p1");
  cr_assert(same_double(d.to_double(), dv));
  cr_assert(d.is_negative());
}

Test(from_string, hex_float_uppercase) {
  double dv = std::strtod("0X1.8P1", nullptr);
  cr_assert_eq(dv, 3.0);
  decimal d = decimal::from_string("0X1.8P1");
  cr_assert(same_double(d.to_double(), dv));
}

Test(from_string, hex_float_negative_exponent) {
  double dv = std::strtod("0x1p-1", nullptr);
  cr_assert_eq(dv, 0.5);
  decimal d = decimal::from_string("0x1p-1");
  cr_assert(same_double(d.to_double(), dv));
}

Test(from_string, hex_float_zero) {
  double dv = std::strtod("0x0p0", nullptr);
  cr_assert_eq(dv, 0.0);
  cr_assert(!std::signbit(dv));
  decimal d = decimal::from_string("0x0p0");
  cr_assert(d.is_zero());
  cr_assert(!d.is_negative());
}

Test(from_string, hex_float_negative_zero) {
  double dv = std::strtod("-0x0p0", nullptr);
  cr_assert_eq(dv, 0.0);
  cr_assert(std::signbit(dv));
  decimal d = decimal::from_string("-0x0p0");
  cr_assert(d.is_zero());
  cr_assert(d.is_negative());
}

Test(from_string, hex_float_endptr) {
  char* dend = nullptr;
  double dv = std::strtod("0x1.8p3xyz", &dend);
  cr_assert_eq(dv, 12.0);
  cr_assert_eq(*dend, 'x');

  const char* end = nullptr;
  decimal d = decimal::from_string("0x1.8p3xyz", &end);
  cr_assert(same_double(d.to_double(), dv));
  cr_assert_eq(*end, 'x');
}

Test(from_string, hex_float_inf) {
  double dv = std::strtod("0x1p1024", nullptr);
  cr_assert(std::isinf(dv));
  cr_assert(!std::signbit(dv));
  decimal d = decimal::from_string("0x1p1024");
  cr_assert(d.is_inf());
  cr_assert(!d.is_negative());
}

Test(from_string, hex_float_no_p_exponent) {
  // Verify strtod behavior first: some implementations accept 0x1.8
  // without a p exponent, others stop at 'x'.
  char* dend = nullptr;
  double dv = std::strtod("0x1.8", &dend);

  const char* end = nullptr;
  decimal d = decimal::from_string("0x1.8", &end);

  // decimal should match whatever strtod does.
  cr_assert(same_double(d.to_double(), dv));
  cr_assert_eq(*end, *dend);
}

// ---------------------------------------------------------------------------
// to_string
// ---------------------------------------------------------------------------

// Helper: assert both to_string() overloads produce the expected result.
#define ASSERT_TO_STRING(d, expected) do {                                     \
  decimal _d = (d);                                                            \
  std::string _str = _d.to_string();                                           \
  cr_assert_str_eq(_str.c_str(), expected);                                    \
  char _buf[decimal::max_string_length + 1];                                   \
  cr_assert_eq(_d.to_string(_buf), _buf,                                       \
               "to_string(char*) should return buf");                           \
  cr_assert_str_eq(_buf, expected);                                            \
} while (0)

Test(to_string, zero) {
  ASSERT_TO_STRING(decimal(), "0");
}

Test(to_string, negative_zero) {
  ASSERT_TO_STRING(decimal::zero(NEGATIVE), "-0");
}

Test(to_string, nan) {
  ASSERT_TO_STRING(decimal::nan(), "nan");
}

Test(to_string, pos_inf) {
  ASSERT_TO_STRING(decimal::inf(), "inf");
}

Test(to_string, neg_inf) {
  ASSERT_TO_STRING(decimal::inf(NEGATIVE), "-inf");
}

Test(to_string, integer) {
  ASSERT_TO_STRING(decimal::from_string("42"), "42");
}

Test(to_string, no_trailing_zeros) {
  ASSERT_TO_STRING(decimal::from_string("1.50"), "1.5");
}

Test(to_string, no_trailing_dot) {
  ASSERT_TO_STRING(decimal::from_string("100"), "100");
}

Test(to_string, no_leading_zeros_integer) {
  ASSERT_TO_STRING(decimal::from_string("007"), "7");
}

Test(to_string, fraction_less_than_one) {
  ASSERT_TO_STRING(decimal::from_string("0.5"), "0.5");
}

Test(to_string, small_fraction) {
  ASSERT_TO_STRING(decimal::from_string("0.000123"), "0.000123");
}

Test(to_string, large_integer) {
  ASSERT_TO_STRING(decimal::from_string("1e10"), "10000000000");
}

Test(to_string, roundtrip_string) {
  const char* cases[] = {"1", "0.1", "0.001", "123.456", "1000000",
                         "99", "0.99", "12345678901234567"};
  for (auto s : cases) {
    ASSERT_TO_STRING(decimal::from_string(s), s);
  }
}

// Negative finite values — exercise the sign path in all four rendering
// branches (integer, decimal-within, pure-fraction, scientific).
Test(to_string, negative_integer) {
  ASSERT_TO_STRING(decimal::from_string("-42"), "-42");
}

Test(to_string, negative_decimal_within) {
  ASSERT_TO_STRING(decimal::from_string("-123.456"), "-123.456");
}

Test(to_string, negative_fraction) {
  ASSERT_TO_STRING(decimal::from_string("-0.5"), "-0.5");
}

Test(to_string, negative_small_fraction) {
  ASSERT_TO_STRING(decimal::from_string("-0.000123"), "-0.000123");
}

Test(to_string, negative_scientific_positive_exp) {
  ASSERT_TO_STRING(decimal::from_string("-1.23e100"), "-1.23e100");
}

Test(to_string, negative_scientific_negative_exp) {
  ASSERT_TO_STRING(decimal::from_string("-4.56e-100"), "-4.56e-100");
}

// Negative roundtrip — mirrors roundtrip_string but with negative sign.
Test(to_string, negative_roundtrip) {
  const char* cases[] = {"-1", "-0.1", "-0.001", "-123.456",
                         "-1000000", "-99", "-0.99", "-12345678901234567"};
  for (auto s : cases) {
    ASSERT_TO_STRING(decimal::from_string(s), s);
  }
}

// ---------------------------------------------------------------------------
// Stream output
// ---------------------------------------------------------------------------

Test(stream, basic) {
  decimal d = decimal::from_string("3.14");
  std::ostringstream oss;
  oss << d;
  cr_assert_str_eq(oss.str().c_str(), "3.14");
}

// ---------------------------------------------------------------------------
// Comparison: basic
// ---------------------------------------------------------------------------

Test(comparison, equal) {
  decimal a = decimal::from_string("1.5");
  decimal b = decimal::from_string("1.5");
  cr_assert(a == b);
  cr_assert(!(a != b));

  cr_assert(1.5 == 1.5);
  cr_assert(!(1.5 != 1.5));
}

Test(comparison, not_equal) {
  decimal a = decimal::from_string("1.5");
  decimal b = decimal::from_string("2.5");
  cr_assert(a != b);
  cr_assert(!(a == b));

  cr_assert(1.5 != 2.5);
}

Test(comparison, less_than) {
  decimal a = decimal::from_string("1");
  decimal b = decimal::from_string("2");
  cr_assert(a < b);
  cr_assert(!(b < a));
  cr_assert(a <= b);
  cr_assert(!(b <= a));

  cr_assert(1.0 < 2.0);
  cr_assert(!(2.0 < 1.0));
}

Test(comparison, greater_than) {
  decimal a = decimal::from_string("10");
  decimal b = decimal::from_string("2");
  cr_assert(a > b);
  cr_assert(!(b > a));
  cr_assert(a >= b);
  cr_assert(!(b >= a));

  cr_assert(10.0 > 2.0);
}

Test(comparison, negative_ordering) {
  decimal a = decimal::from_string("-5");
  decimal b = decimal::from_string("-3");
  cr_assert(a < b);
  cr_assert(b > a);

  cr_assert(-5.0 < -3.0);
}

Test(comparison, negative_vs_positive) {
  decimal a = decimal::from_string("-1");
  decimal b = decimal::from_string("1");
  cr_assert(a < b);
  cr_assert(b > a);

  cr_assert(-1.0 < 1.0);
}

Test(comparison, zero_equal) {
  decimal a = decimal::zero(POSITIVE);
  decimal b = decimal::zero(NEGATIVE);
  cr_assert(a == b);
  cr_assert(!(a < b));
  cr_assert(!(a > b));

  // Verify double: +0.0 == -0.0.
  cr_assert(0.0 == -0.0);
  cr_assert(!(0.0 < -0.0));
  cr_assert(!(0.0 > -0.0));
}

Test(comparison, inf_ordering) {
  decimal pinf = decimal::inf(POSITIVE);
  decimal ninf = decimal::inf(NEGATIVE);
  decimal one = decimal::from_string("1");
  cr_assert(ninf < one);
  cr_assert(one < pinf);
  cr_assert(ninf < pinf);
  cr_assert(pinf == pinf);
  cr_assert(ninf == ninf);

  // Verify double.
  cr_assert(-dinf < 1.0);
  cr_assert(1.0 < dinf);
  cr_assert(-dinf < dinf);
  cr_assert(dinf == dinf);
  cr_assert(-dinf == -dinf);
}

Test(comparison, equal_different_representation) {
  decimal a = decimal::from_string("1.0");
  decimal b = decimal::from_string("1");
  cr_assert(a == b);
}

Test(comparison, different_magnitudes) {
  decimal a = decimal::from_string("999");
  decimal b = decimal::from_string("1000");
  cr_assert(a < b);
}

Test(comparison, same_order_different_digits) {
  decimal a = decimal::from_string("1.5");
  decimal b = decimal::from_string("1.50001");
  cr_assert(a < b);
}

// ---------------------------------------------------------------------------
// NaN semantics
// ---------------------------------------------------------------------------

Test(nan_semantics, nan_not_equal_to_self) {
  decimal n = decimal::nan();
  cr_assert(!(n == n));
  cr_assert(n != n);

  cr_assert(!(dnan == dnan));
  cr_assert(dnan != dnan);
}

Test(nan_semantics, nan_not_less) {
  decimal n = decimal::nan();
  decimal one = decimal::from_string("1");
  cr_assert(!(n < one));
  cr_assert(!(one < n));

  cr_assert(!(dnan < 1.0));
  cr_assert(!(1.0 < dnan));
}

Test(nan_semantics, nan_not_greater) {
  decimal n = decimal::nan();
  decimal one = decimal::from_string("1");
  cr_assert(!(n > one));
  cr_assert(!(one > n));

  cr_assert(!(dnan > 1.0));
  cr_assert(!(1.0 > dnan));
}

Test(nan_semantics, nan_not_leq) {
  decimal n = decimal::nan();
  decimal one = decimal::from_string("1");
  cr_assert(!(n <= one));
  cr_assert(!(one <= n));

  cr_assert(!(dnan <= 1.0));
  cr_assert(!(1.0 <= dnan));
}

Test(nan_semantics, nan_not_geq) {
  decimal n = decimal::nan();
  decimal one = decimal::from_string("1");
  cr_assert(!(n >= one));
  cr_assert(!(one >= n));

  cr_assert(!(dnan >= 1.0));
  cr_assert(!(1.0 >= dnan));
}

Test(nan_semantics, nan_not_equal_to_nan) {
  cr_assert(!(decimal::nan() == decimal::nan()));
  cr_assert(!(dnan == dnan));
}

Test(nan_semantics, nan_neq_nan) {
  cr_assert(decimal::nan() != decimal::nan());
  cr_assert(dnan != dnan);
}

Test(nan_semantics, matches_double) {
  decimal n = decimal::nan();
  decimal one = decimal::from_string("1");

  cr_assert_eq(n == n, dnan == dnan);
  cr_assert_eq(n != n, dnan != dnan);
  cr_assert_eq(n < one, dnan < 1.0);
  cr_assert_eq(n > one, dnan > 1.0);
  cr_assert_eq(n <= one, dnan <= 1.0);
  cr_assert_eq(n >= one, dnan >= 1.0);
}

// ---------------------------------------------------------------------------
// Arithmetic: addition
// ---------------------------------------------------------------------------

Test(addition, simple) {
  decimal a = decimal::from_string("1.5");
  decimal b = decimal::from_string("2.5");
  decimal c = a + b;
  ASSERT_TOSTR_EQ(c, "4");

  cr_assert_eq(1.5 + 2.5, 4.0);
}

Test(addition, different_exponents) {
  decimal a = decimal::from_string("1000");
  decimal b = decimal::from_string("0.001");
  decimal c = a + b;
  ASSERT_TOSTR_EQ(c, "1000.001");

  cr_assert_eq(1000.0 + 0.001, 1000.001);
}

Test(addition, negative_result) {
  decimal a = decimal::from_string("3");
  decimal b = decimal::from_string("-5");
  decimal c = a + b;
  ASSERT_TOSTR_EQ(c, "-2");

  cr_assert_eq(3.0 + (-5.0), -2.0);
}

Test(addition, cancel_to_zero) {
  decimal a = decimal::from_string("7.7");
  decimal b = decimal::from_string("-7.7");
  decimal c = a + b;
  cr_assert(c.is_zero());

  cr_assert_eq(7.7 + (-7.7), 0.0);
}

Test(addition, inf_plus_inf) {
  decimal c = decimal::inf() + decimal::inf();
  cr_assert(c.is_inf());
  cr_assert(!c.is_negative());

  double dr = dinf + dinf;
  cr_assert(std::isinf(dr));
  cr_assert(dr > 0);
}

Test(addition, inf_plus_neg_inf) {
  decimal c = decimal::inf() + decimal::inf(NEGATIVE);
  cr_assert(c.is_nan());

  cr_assert(std::isnan(dinf + (-dinf)));
}

Test(addition, nan_propagates) {
  decimal c = decimal::nan() + decimal::from_string("1");
  cr_assert(c.is_nan());

  cr_assert(std::isnan(dnan + 1.0));
}

Test(addition, zero_plus_zero) {
  decimal c = decimal::zero() + decimal::zero();
  cr_assert(c.is_zero());

  cr_assert_eq(0.0 + 0.0, 0.0);
}

Test(addition, neg_zero_plus_neg_zero) {
  decimal c = decimal::zero(NEGATIVE) + decimal::zero(NEGATIVE);
  cr_assert(c.is_zero());
  cr_assert(c.is_negative());

  // Verify double: -0 + -0 = -0.
  double dr = -0.0 + (-0.0);
  cr_assert_eq(dr, 0.0);
  cr_assert(std::signbit(dr));
}

Test(addition, pos_zero_plus_neg_zero) {
  decimal c = decimal::zero(POSITIVE) + decimal::zero(NEGATIVE);
  cr_assert(c.is_zero());
  cr_assert(!c.is_negative());

  // Verify double: +0 + -0 = +0.
  double dr = 0.0 + (-0.0);
  cr_assert_eq(dr, 0.0);
  cr_assert(!std::signbit(dr));
}

Test(addition, commutative) {
  decimal a = decimal::from_string("123.456");
  decimal b = decimal::from_string("789.012");
  cr_assert((a + b) == (b + a));
}

// ---------------------------------------------------------------------------
// Arithmetic: subtraction
// ---------------------------------------------------------------------------

Test(subtraction, simple) {
  decimal a = decimal::from_string("10");
  decimal b = decimal::from_string("3");
  ASSERT_TOSTR_EQ((a - b), "7");

  cr_assert_eq(10.0 - 3.0, 7.0);
}

Test(subtraction, result_negative) {
  decimal a = decimal::from_string("3");
  decimal b = decimal::from_string("10");
  ASSERT_TOSTR_EQ((a - b), "-7");

  cr_assert_eq(3.0 - 10.0, -7.0);
}

Test(subtraction, self_is_zero) {
  decimal a = decimal::from_string("42");
  cr_assert((a - a).is_zero());

  cr_assert_eq(42.0 - 42.0, 0.0);
}

// ---------------------------------------------------------------------------
// Arithmetic: multiplication
// ---------------------------------------------------------------------------

Test(multiplication, simple) {
  decimal a = decimal::from_string("6");
  decimal b = decimal::from_string("7");
  ASSERT_TOSTR_EQ((a * b), "42");

  cr_assert_eq(6.0 * 7.0, 42.0);
}

Test(multiplication, decimal) {
  decimal a = decimal::from_string("1.5");
  decimal b = decimal::from_string("4");
  ASSERT_TOSTR_EQ((a * b), "6");

  cr_assert_eq(1.5 * 4.0, 6.0);
}

Test(multiplication, negative) {
  decimal a = decimal::from_string("-3");
  decimal b = decimal::from_string("5");
  ASSERT_TOSTR_EQ((a * b), "-15");

  cr_assert_eq(-3.0 * 5.0, -15.0);
}

Test(multiplication, neg_times_neg) {
  decimal a = decimal::from_string("-3");
  decimal b = decimal::from_string("-5");
  ASSERT_TOSTR_EQ((a * b), "15");

  cr_assert_eq(-3.0 * -5.0, 15.0);
}

Test(multiplication, by_zero) {
  decimal a = decimal::from_string("42");
  decimal b = decimal::zero();
  cr_assert((a * b).is_zero());

  cr_assert_eq(42.0 * 0.0, 0.0);
}

Test(multiplication, zero_times_inf) {
  cr_assert((decimal::zero() * decimal::inf()).is_nan());

  cr_assert(std::isnan(0.0 * dinf));
}

Test(multiplication, inf_times_finite) {
  decimal c = decimal::inf() * decimal::from_string("5");
  cr_assert(c.is_inf());
  cr_assert(!c.is_negative());

  double dr = dinf * 5.0;
  cr_assert(std::isinf(dr));
  cr_assert(dr > 0);
}

Test(multiplication, inf_times_neg) {
  decimal c = decimal::inf() * decimal::from_string("-5");
  cr_assert(c.is_inf());
  cr_assert(c.is_negative());

  double dr = dinf * -5.0;
  cr_assert(std::isinf(dr));
  cr_assert(dr < 0);
}

Test(multiplication, nan_propagates) {
  cr_assert((decimal::nan() * decimal::from_string("1")).is_nan());

  cr_assert(std::isnan(dnan * 1.0));
}

// ---------------------------------------------------------------------------
// Arithmetic: division
// ---------------------------------------------------------------------------

Test(division, simple) {
  decimal a = decimal::from_string("10");
  decimal b = decimal::from_string("2");
  ASSERT_TOSTR_EQ((a / b), "5");

  cr_assert_eq(10.0 / 2.0, 5.0);
}

Test(division, decimal_result) {
  decimal a = decimal::from_string("1");
  decimal b = decimal::from_string("4");
  ASSERT_TOSTR_EQ((a / b), "0.25");

  cr_assert_eq(1.0 / 4.0, 0.25);
}

Test(division, negative) {
  decimal a = decimal::from_string("10");
  decimal b = decimal::from_string("-2");
  ASSERT_TOSTR_EQ((a / b), "-5");

  cr_assert_eq(10.0 / -2.0, -5.0);
}

Test(division, by_zero_nonzero) {
  decimal c = decimal::from_string("1") / decimal::zero();
  cr_assert(c.is_inf());
  cr_assert(!c.is_negative());

  double dr = 1.0 / 0.0;
  cr_assert(std::isinf(dr));
  cr_assert(dr > 0);
}

Test(division, by_zero_negative) {
  decimal c = decimal::from_string("-1") / decimal::zero();
  cr_assert(c.is_inf());
  cr_assert(c.is_negative());

  double dr = -1.0 / 0.0;
  cr_assert(std::isinf(dr));
  cr_assert(dr < 0);
}

Test(division, zero_by_zero) {
  cr_assert((decimal::zero() / decimal::zero()).is_nan());

  cr_assert(std::isnan(0.0 / 0.0));
}

Test(division, inf_by_inf) {
  cr_assert((decimal::inf() / decimal::inf()).is_nan());

  cr_assert(std::isnan(dinf / dinf));
}

Test(division, finite_by_inf) {
  decimal c = decimal::from_string("1") / decimal::inf();
  cr_assert(c.is_zero());

  cr_assert_eq(1.0 / dinf, 0.0);
}

Test(division, inf_by_finite) {
  decimal c = decimal::inf() / decimal::from_string("2");
  cr_assert(c.is_inf());

  cr_assert(std::isinf(dinf / 2.0));
}

Test(division, nan_propagates) {
  cr_assert((decimal::nan() / decimal::from_string("1")).is_nan());
  cr_assert((decimal::from_string("1") / decimal::nan()).is_nan());

  cr_assert(std::isnan(dnan / 1.0));
  cr_assert(std::isnan(1.0 / dnan));
}

Test(division, one_third) {
  decimal a = decimal::from_string("1");
  decimal b = decimal::from_string("3");
  decimal c = a / b;
  std::string s = c.to_string();
  cr_assert(s.find("0.333333333") == 0,
            "1/3 should start with 0.333333333, got %s", s.c_str());
}

// ---------------------------------------------------------------------------
// Unary negation and abs
// ---------------------------------------------------------------------------

Test(unary, negation) {
  decimal a = decimal::from_string("5");
  decimal b = -a;
  cr_assert(b.is_negative());
  ASSERT_TOSTR_EQ(b, "-5");

  cr_assert_eq(-5.0, -(5.0));
}

Test(unary, double_negation) {
  decimal a = decimal::from_string("5");
  cr_assert((-(-a)) == a);

  cr_assert_eq(-(-5.0), 5.0);
}

Test(unary, negation_of_nan) {
  decimal n = -decimal::nan();
  cr_assert(n.is_nan());

  cr_assert(std::isnan(-dnan));
}

Test(unary, negation_of_inf) {
  decimal n = -decimal::inf();
  cr_assert(n.is_inf());
  cr_assert(n.is_negative());

  double dr = -dinf;
  cr_assert(std::isinf(dr));
  cr_assert(dr < 0);
}

Test(unary, abs_positive) {
  decimal a = decimal::from_string("5");
  cr_assert(a.abs() == a);

  cr_assert_eq(std::abs(5.0), 5.0);
}

Test(unary, abs_negative) {
  decimal a = decimal::from_string("-5");
  ASSERT_TOSTR_EQ(a.abs(), "5");

  cr_assert_eq(std::abs(-5.0), 5.0);
}

Test(unary, abs_neg_zero) {
  decimal a = decimal::zero(NEGATIVE);
  cr_assert(a.abs().is_zero());
  cr_assert(!a.abs().is_negative());

  // Verify double: abs(-0) = +0.
  double dr = std::abs(-0.0);
  cr_assert_eq(dr, 0.0);
  cr_assert(!std::signbit(dr));
}

// ---------------------------------------------------------------------------
// Compound assignment
// ---------------------------------------------------------------------------

Test(compound, plus_eq) {
  decimal a = decimal::from_string("10");
  a += decimal::from_string("5");
  ASSERT_TOSTR_EQ(a, "15");
}

Test(compound, minus_eq) {
  decimal a = decimal::from_string("10");
  a -= decimal::from_string("3");
  ASSERT_TOSTR_EQ(a, "7");
}

Test(compound, times_eq) {
  decimal a = decimal::from_string("6");
  a *= decimal::from_string("7");
  ASSERT_TOSTR_EQ(a, "42");
}

Test(compound, div_eq) {
  decimal a = decimal::from_string("10");
  a /= decimal::from_string("4");
  ASSERT_TOSTR_EQ(a, "2.5");
}

// ---------------------------------------------------------------------------
// Double conversion round-trip
// ---------------------------------------------------------------------------

Test(double_conv, round_trip_simple) {
  double vals[] = {0.0, 1.0, -1.0, 0.5, 100.0, 1e10, 1e-10, 3.14};
  for (double v : vals) {
    decimal d(v);
    double back = d.to_double();
    cr_assert(same_double(v, back),
              "round-trip failed for %g: got %g", v, back);
  }
}

Test(double_conv, round_trip_special) {
  cr_assert(std::isnan(decimal(dnan).to_double()));
  cr_assert(decimal(dinf).to_double() == dinf);
  cr_assert(decimal(-dinf).to_double() == -dinf);
}

Test(double_conv, round_trip_neg_zero) {
  decimal d(-0.0);
  double back = d.to_double();
  cr_assert(std::signbit(back));
  cr_assert(back == 0.0);
}

Test(double_conv, large_double) {
  double v = 1.7976931348623157e+308;
  decimal d(v);
  double back = d.to_double();
  cr_assert(same_double(v, back));
}

Test(double_conv, small_double) {
  double v = 5e-324;
  decimal d(v);
  double back = d.to_double();
  cr_assert(same_double(v, back));
}

// ---------------------------------------------------------------------------
// Verify decimal behaves like double for special operations
// ---------------------------------------------------------------------------

static void check_matches_double_binop(double a, double b, const char* op) {
  decimal da(a), db(b);
  double dr;
  decimal result = decimal::zero();

  if (std::strcmp(op, "+") == 0) {
    dr = a + b;
    result = da + db;
  } else if (std::strcmp(op, "-") == 0) {
    dr = a - b;
    result = da - db;
  } else if (std::strcmp(op, "*") == 0) {
    dr = a * b;
    result = da * db;
  } else {
    dr = a / b;
    result = da / db;
  }

  if (std::isnan(dr)) {
    cr_assert(result.is_nan(),
              "%g %s %g: expected NaN, got %s", a, op, b,
              result.to_string().c_str());
  } else if (std::isinf(dr)) {
    cr_assert(result.is_inf(),
              "%g %s %g: expected inf, got %s", a, op, b,
              result.to_string().c_str());
    cr_assert_eq(result.is_negative(), dr < 0);
  } else if (dr == 0.0) {
    cr_assert(result.is_zero(),
              "%g %s %g: expected zero, got %s", a, op, b,
              result.to_string().c_str());
  }
}

Test(double_behavior, special_add) {
  double vals[] = {0.0, -0.0, 1.0, -1.0, dinf, -dinf, dnan};

  for (double a : vals)
    for (double b : vals)
      check_matches_double_binop(a, b, "+");
}

Test(double_behavior, special_sub) {
  double vals[] = {0.0, -0.0, 1.0, -1.0, dinf, -dinf, dnan};

  for (double a : vals)
    for (double b : vals)
      check_matches_double_binop(a, b, "-");
}

Test(double_behavior, special_mul) {
  double vals[] = {0.0, -0.0, 1.0, -1.0, dinf, -dinf, dnan};

  for (double a : vals)
    for (double b : vals)
      check_matches_double_binop(a, b, "*");
}

Test(double_behavior, special_div) {
  double vals[] = {0.0, -0.0, 1.0, -1.0, dinf, -dinf, dnan};

  for (double a : vals)
    for (double b : vals)
      check_matches_double_binop(a, b, "/");
}

Test(double_behavior, comparison_matches) {
  double vals[] = {0.0, -0.0, 1.0, -1.0, 100.0, dinf, -dinf, dnan};

  for (double a : vals) {
    for (double b : vals) {
      decimal da(a), db(b);
      cr_assert_eq(da == db, a == b,
                   "%g == %g: decimal says %d, double says %d",
                   a, b, da == db, a == b);
      cr_assert_eq(da != db, a != b,
                   "%g != %g: decimal says %d, double says %d",
                   a, b, da != db, a != b);
      cr_assert_eq(da < db, a < b,
                   "%g < %g: decimal says %d, double says %d",
                   a, b, da < db, a < b);
      cr_assert_eq(da <= db, a <= b,
                   "%g <= %g: decimal says %d, double says %d",
                   a, b, da <= db, a <= b);
      cr_assert_eq(da > db, a > b,
                   "%g > %g: decimal says %d, double says %d",
                   a, b, da > db, a > b);
      cr_assert_eq(da >= db, a >= b,
                   "%g >= %g: decimal says %d, double says %d",
                   a, b, da >= db, a >= b);
    }
  }
}

// ---------------------------------------------------------------------------
// Edge cases: exponent range beyond double
// ---------------------------------------------------------------------------

Test(extended_range, large_exponent) {
  decimal d = decimal::from_string("1e3000");
  cr_assert(!d.is_inf());
  cr_assert(!d.is_nan());
  cr_assert_eq(d.mantissa(), 1000000000000000000ULL);
  cr_assert_eq(d.exponent(), 2982);
}

Test(extended_range, small_exponent) {
  decimal d = decimal::from_string("1e-3000");
  cr_assert(!d.is_zero());
  cr_assert_eq(d.mantissa(), 1000000000000000000ULL);
  cr_assert_eq(d.exponent(), -3018);
}

Test(extended_range, multiply_large) {
  decimal a = decimal::from_string("1e2000");
  decimal b = decimal::from_string("1e1000");
  decimal c = a * b;
  cr_assert(!c.is_inf());
  cr_assert_eq(c.exponent(), 2982);
}

Test(extended_range, add_large) {
  decimal a = decimal::from_string("5e3000");
  decimal b = decimal::from_string("3e3000");
  decimal c = a + b;
  ASSERT_TOSTR_EQ(c, "8e3000");
}

Test(extended_range, exponent_well_beyond_double) {
  decimal a = decimal::from_string("1e1000000");
  decimal b = decimal::from_string("1e1000000");
  decimal c = a * b;
  cr_assert(!c.is_inf());
  cr_assert_eq(c.exponent(), 1999982);
}

Test(extended_range, overflow_to_inf) {
  decimal a = decimal::from_string("1e3000000000000000000");
  decimal b = decimal::from_string("1e3000000000000000000");
  decimal c = a * b;
  cr_assert(c.is_inf());
}

Test(extended_range, underflow_to_zero) {
  decimal a = decimal::from_string("1e-3000000000000000000");
  decimal b = decimal::from_string("1e-3000000000000000000");
  decimal c = a * b;
  cr_assert(c.is_zero());
}

Test(extended_range, to_double_large) {
  decimal d = decimal::from_string("1e3000");
  double v = d.to_double();
  cr_assert(std::isinf(v));
}

Test(extended_range, to_double_small) {
  decimal d = decimal::from_string("1e-3000");
  double v = d.to_double();
  cr_assert_eq(v, 0.0);
}

// ---------------------------------------------------------------------------
// Additional arithmetic edge cases
// ---------------------------------------------------------------------------

Test(arithmetic_edge, add_very_different_magnitudes) {
  decimal a = decimal::from_string("1e18");
  decimal b = decimal::from_string("1e-5");
  decimal c = a + b;
  cr_assert(c == a);
}

Test(arithmetic_edge, sub_nearly_equal) {
  decimal a = decimal::from_string("1.000000000000001");
  decimal b = decimal::from_string("1");
  decimal c = a - b;
  std::string s = c.to_string();
  cr_assert_str_eq(s.c_str(), "0.000000000000001");
}

Test(arithmetic_edge, mul_by_one) {
  decimal a = decimal::from_string("123.456");
  decimal b = decimal::from_string("1");
  cr_assert((a * b) == a);

  cr_assert_eq(123.456 * 1.0, 123.456);
}

Test(arithmetic_edge, div_by_one) {
  decimal a = decimal::from_string("123.456");
  decimal b = decimal::from_string("1");
  cr_assert((a / b) == a);

  cr_assert_eq(123.456 / 1.0, 123.456);
}

Test(arithmetic_edge, div_one_by_self) {
  decimal a = decimal::from_string("7");
  decimal c = a / a;
  ASSERT_TOSTR_EQ(c, "1");

  cr_assert_eq(7.0 / 7.0, 1.0);
}

Test(arithmetic_edge, large_mantissa_add) {
  decimal a = decimal::from_string("9999999999999999999");
  decimal b = decimal::from_string("1");
  decimal c = a + b;
  cr_assert_eq(c.mantissa(), 1000000000000000000ULL);
  cr_assert_eq(c.exponent(), 1);
}

Test(arithmetic_edge, large_mantissa_mul) {
  decimal a = decimal::from_string("9999999999");
  decimal b = decimal::from_string("9999999999");
  decimal c = a * b;
  cr_assert(!c.is_nan());
  cr_assert(!c.is_inf());
}

// ---------------------------------------------------------------------------
// from_string with std::string overload
// ---------------------------------------------------------------------------

Test(from_string_std, basic) {
  std::string s = "42.5";
  decimal d = decimal::from_string(s);
  ASSERT_TOSTR_EQ(d, "42.5");
}

// ---------------------------------------------------------------------------
// std::numeric_limits
// ---------------------------------------------------------------------------

Test(numeric_limits, is_specialized) {
  cr_assert(std::numeric_limits<decimal>::is_specialized);
  cr_assert(std::numeric_limits<decimal>::is_signed);
  cr_assert(!std::numeric_limits<decimal>::is_integer);
  cr_assert(!std::numeric_limits<decimal>::is_exact);
  cr_assert(std::numeric_limits<decimal>::has_infinity);
  cr_assert(std::numeric_limits<decimal>::has_quiet_NaN);
  cr_assert_eq(std::numeric_limits<decimal>::radix, 10);
  cr_assert_eq(std::numeric_limits<decimal>::digits, 19);
  cr_assert_eq(std::numeric_limits<decimal>::digits10, 19);
  cr_assert_eq(std::numeric_limits<decimal>::max_digits10, 19);
}

Test(numeric_limits, infinity_and_nan) {
  cr_assert(std::numeric_limits<decimal>::infinity().is_inf());
  cr_assert(!std::numeric_limits<decimal>::infinity().is_negative());
  cr_assert(std::numeric_limits<decimal>::quiet_NaN().is_nan());
}

Test(numeric_limits, max_is_finite) {
  decimal m = std::numeric_limits<decimal>::max();
  cr_assert(m.is_finite());
  cr_assert(!m.is_negative());
  cr_assert(!m.is_zero());
  cr_assert_eq(m.mantissa(), 9999999999999999999ULL);
}

Test(numeric_limits, lowest_is_negative_max) {
  decimal lo = std::numeric_limits<decimal>::lowest();
  decimal hi = std::numeric_limits<decimal>::max();
  cr_assert(lo.is_negative());
  cr_assert(lo.is_finite());
  cr_assert_eq(lo.mantissa(), hi.mantissa());
  cr_assert_eq(lo.exponent(), hi.exponent());
}

Test(numeric_limits, min_is_smallest_positive) {
  decimal m = std::numeric_limits<decimal>::min();
  cr_assert(!m.is_negative());
  cr_assert(!m.is_zero());
  cr_assert(m.is_finite());
  cr_assert_eq(m.mantissa(), 1000000000000000000ULL);
  decimal small = decimal::from_string("1e-3000");
  cr_assert(m < small);
}

Test(numeric_limits, epsilon) {
  decimal eps = std::numeric_limits<decimal>::epsilon();
  decimal one = decimal::from_string("1");
  decimal result = one + eps;
  cr_assert(result != one, "1 + epsilon should differ from 1");
  decimal small = decimal::from_string("1e-19");
  decimal result2 = one + small;
  cr_assert(result2 == one, "1 + 1e-19 should equal 1");

  // Verify the analogous property for double.
  double deps = std::numeric_limits<double>::epsilon();
  cr_assert(1.0 + deps != 1.0);
  cr_assert(1.0 + deps / 4.0 == 1.0);
}

Test(numeric_limits, ordering) {
  decimal lo = std::numeric_limits<decimal>::lowest();
  decimal mi = std::numeric_limits<decimal>::min();
  decimal mx = std::numeric_limits<decimal>::max();
  cr_assert(lo < mi);
  cr_assert(mi < mx);
  cr_assert(lo < decimal::from_string("-1"));
  cr_assert(mx > decimal::from_string("1e3000"));

  // Verify analogous double ordering.
  cr_assert(std::numeric_limits<double>::lowest() <
            std::numeric_limits<double>::min());
  cr_assert(std::numeric_limits<double>::min() <
            std::numeric_limits<double>::max());
}

// ---------------------------------------------------------------------------
// sign enum
// ---------------------------------------------------------------------------

Test(sign_enum, inf_positive_default) {
  decimal d = decimal::inf();
  cr_assert(!d.is_negative());
}

Test(sign_enum, inf_positive_explicit) {
  decimal d = decimal::inf(POSITIVE);
  cr_assert(!d.is_negative());
}

Test(sign_enum, inf_negative_explicit) {
  decimal d = decimal::inf(NEGATIVE);
  cr_assert(d.is_negative());
}

Test(sign_enum, zero_positive_default) {
  decimal d = decimal::zero();
  cr_assert(!d.is_negative());
}

Test(sign_enum, zero_negative_explicit) {
  decimal d = decimal::zero(NEGATIVE);
  cr_assert(d.is_negative());
}

// ---------------------------------------------------------------------------
// Scientific notation and max_string_length
// ---------------------------------------------------------------------------

Test(to_string, scientific_large_positive_exp) {
  ASSERT_TO_STRING(decimal::from_string("1.23e100"), "1.23e100");
}

Test(to_string, scientific_large_negative_exp) {
  ASSERT_TO_STRING(decimal::from_string("4.56e-100"), "4.56e-100");
}

Test(to_string, scientific_single_digit) {
  ASSERT_TO_STRING(decimal::from_string("7e3000"), "7e3000");
}

Test(to_string, scientific_many_digits) {
  ASSERT_TO_STRING(decimal::from_string("1.234567890123456789e1000"),
                   "1.234567890123456789e1000");
}

Test(to_string, plain_within_threshold) {
  ASSERT_TO_STRING(decimal::from_string("1e19"), "10000000000000000000");
}

Test(to_string, scientific_just_beyond_threshold) {
  ASSERT_TO_STRING(decimal::from_string("1e20"), "1e20");
}

Test(to_string, plain_small_negative_dp) {
  std::string expected = "0." + std::string(20, '0') + "1";
  ASSERT_TO_STRING(decimal::from_string("1e-21"), expected.c_str());
}

Test(to_string, scientific_small_beyond_threshold) {
  ASSERT_TO_STRING(decimal::from_string("1e-22"), "1e-22");
}

Test(to_string, scientific_roundtrip) {
  const char* cases[] = {"1.23e100", "4.56e-100", "1e3000", "9.99e-3000",
                         "1.234567890123456789e1000"};
  for (auto s : cases) {
    ASSERT_TO_STRING(decimal::from_string(s), s);
  }
}

// Threshold boundary with a full 19-digit significand.
// dp = PLAIN_DP_LIMIT (20): still plain.
Test(to_string, plain_threshold_full_significand) {
  // 1.234567890123456789e19 = 12345678901234567890 (dp=20, plain)
  ASSERT_TO_STRING(decimal::from_string("1.234567890123456789e19"),
                   "12345678901234567890");
}

// dp = PLAIN_DP_LIMIT + 1 (21): switches to scientific.
Test(to_string, scientific_threshold_full_significand) {
  // 1.234567890123456789e20 → dp=21 > 20
  ASSERT_TO_STRING(decimal::from_string("1.234567890123456789e20"),
                   "1.234567890123456789e20");
}

// dp = -PLAIN_DP_LIMIT (-20): still plain (pure fraction).
Test(to_string, plain_small_threshold_full_significand) {
  // 1.234567890123456789e-21 → dp=-20, plain: 0. followed by 20 zeros then digits
  std::string expected = "0." + std::string(20, '0') + "1234567890123456789";
  ASSERT_TO_STRING(decimal::from_string("1.234567890123456789e-21"),
                   expected.c_str());
}

// dp = -PLAIN_DP_LIMIT - 1 (-21): switches to scientific.
Test(to_string, scientific_small_threshold_full_significand) {
  // 1.234567890123456789e-22 → dp=-21 < -20
  ASSERT_TO_STRING(decimal::from_string("1.234567890123456789e-22"),
                   "1.234567890123456789e-22");
}

// ---------------------------------------------------------------------------
// operator bool
// ---------------------------------------------------------------------------

Test(operator_bool, zero_is_false) {
  cr_assert(!static_cast<bool>(decimal::zero()));
  cr_assert(!static_cast<bool>(decimal::zero(NEGATIVE)));
  // double: (bool)0.0 == false, (bool)(-0.0) == false
  cr_assert(!(bool)0.0);
  cr_assert(!(bool)(-0.0));
}

Test(operator_bool, nonzero_is_true) {
  cr_assert(static_cast<bool>(decimal::from_string("1")));
  cr_assert(static_cast<bool>(decimal::from_string("-1")));
  cr_assert(static_cast<bool>(decimal::from_string("0.001")));
  cr_assert(static_cast<bool>(decimal::from_string("1e300")));
}

Test(operator_bool, nan_is_true) {
  cr_assert(static_cast<bool>(decimal::nan()));
  cr_assert((bool)dnan);
}

Test(operator_bool, inf_is_true) {
  cr_assert(static_cast<bool>(decimal::inf()));
  cr_assert(static_cast<bool>(decimal::inf(NEGATIVE)));
  cr_assert((bool)dinf);
  cr_assert((bool)(-dinf));
}

Test(operator_bool, if_and_not) {
  decimal nonzero = decimal::from_string("42");
  decimal z = decimal::zero();
  if (nonzero) {} else { cr_assert_fail("nonzero should be truthy"); }
  if (!z) {} else { cr_assert_fail("zero should be falsy"); }
}

// ---------------------------------------------------------------------------
// operator double
// ---------------------------------------------------------------------------

Test(operator_double, matches_to_double) {
  const char* cases[] = {
      "0", "1", "-1", "3.14159", "1e100", "-1e-100",
      "9999999999999999999", "nan", "inf", "-inf", "0.001"};
  for (auto s : cases) {
    decimal d = decimal::from_string(s);
    cr_assert(same_double(static_cast<double>(d), d.to_double()),
              "operator double != to_double for \"%s\"", s);
  }
}

// ---------------------------------------------------------------------------
// operator int64_t
// ---------------------------------------------------------------------------

Test(operator_int64, basic_values) {
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("0")), 0);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("42")), 42);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("-42")), -42);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("1")), 1);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("-1")), -1);
}

Test(operator_int64, truncation) {
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("3.9")), 3);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("-3.9")), -3);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("0.999")), 0);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("-0.001")), 0);
}

Test(operator_int64, large_values) {
  // Near INT64_MAX
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("9223372036854775807")),
               INT64_MAX);
  // Overflow saturates
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("9223372036854775808")),
               INT64_MAX);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("1e19")), INT64_MAX);
  // Near INT64_MIN
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("-9223372036854775808")),
               INT64_MIN);
  cr_assert_eq(static_cast<int64_t>(decimal::from_string("-1e19")), INT64_MIN);
}

Test(operator_int64, special_values) {
  cr_assert_eq(static_cast<int64_t>(decimal::nan()), 0);
  cr_assert_eq(static_cast<int64_t>(decimal::inf()), INT64_MAX);
  cr_assert_eq(static_cast<int64_t>(decimal::inf(NEGATIVE)), INT64_MIN);
}

// ---------------------------------------------------------------------------
// operator uint64_t
// ---------------------------------------------------------------------------

Test(operator_uint64, basic_values) {
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("0")), 0u);
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("42")), 42u);
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("1")), 1u);
}

Test(operator_uint64, negative_is_zero) {
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("-1")), 0u);
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("-1000")), 0u);
}

Test(operator_uint64, large_values) {
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("18446744073709551615")),
               UINT64_MAX);
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("18446744073709551616")),
               UINT64_MAX);  // overflow saturates
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("1e20")), UINT64_MAX);
}

Test(operator_uint64, truncation) {
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("9.9")), 9u);
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("0.5")), 0u);
}

Test(operator_uint64, special_values) {
  cr_assert_eq(static_cast<uint64_t>(decimal::nan()), 0u);
  cr_assert_eq(static_cast<uint64_t>(decimal::inf()), UINT64_MAX);
  cr_assert_eq(static_cast<uint64_t>(decimal::inf(NEGATIVE)), 0u);
}

// ---------------------------------------------------------------------------
// mul_pow10
// ---------------------------------------------------------------------------

Test(mul_pow10, basic) {
  decimal d = decimal::from_string("1.5");
  ASSERT_TOSTR_EQ(d.mul_pow10(0), "1.5");
  ASSERT_TOSTR_EQ(d.mul_pow10(1), "15");
  ASSERT_TOSTR_EQ(d.mul_pow10(2), "150");
  ASSERT_TOSTR_EQ(d.mul_pow10(-1), "0.15");
  ASSERT_TOSTR_EQ(d.mul_pow10(-3), "0.0015");
}

Test(mul_pow10, preserves_mantissa) {
  decimal d = decimal::from_string("1234567890123456789");
  decimal shifted = d.mul_pow10(5);
  cr_assert_eq(shifted.mantissa(), d.mantissa());
  cr_assert_eq(shifted.exponent(), d.exponent() + 5);
}

Test(mul_pow10, overflow_to_inf) {
  decimal d = decimal::from_string("1e4000000000000000000");
  decimal result = d.mul_pow10(1000000000000000000LL);
  cr_assert(result.is_inf());
  cr_assert(!result.is_negative());
}

Test(mul_pow10, underflow_to_zero) {
  decimal d = decimal::from_string("1e-4000000000000000000");
  decimal result = d.mul_pow10(-1000000000000000000LL);
  cr_assert(result.is_zero());
}

Test(mul_pow10, special_passthrough) {
  cr_assert(decimal::nan().mul_pow10(5).is_nan());
  cr_assert(decimal::inf().mul_pow10(5).is_inf());
  cr_assert(decimal::zero().mul_pow10(5).is_zero());
}

Test(mul_pow10, extreme_p) {
  decimal d = decimal::from_string("1");
  cr_assert(d.mul_pow10(INT64_MAX).is_inf());
  cr_assert(d.mul_pow10(INT64_MIN).is_zero());
}

// ---------------------------------------------------------------------------
// pow10
// ---------------------------------------------------------------------------

Test(pow10, basic) {
  ASSERT_TOSTR_EQ(decimal::pow10(0), "1");
  ASSERT_TOSTR_EQ(decimal::pow10(1), "10");
  ASSERT_TOSTR_EQ(decimal::pow10(2), "100");
  ASSERT_TOSTR_EQ(decimal::pow10(-1), "0.1");
  ASSERT_TOSTR_EQ(decimal::pow10(-3), "0.001");
  std::string s = decimal::pow10(18).to_string();
  cr_assert_str_eq(s.c_str(), "1000000000000000000");
}

Test(pow10, large_exponents) {
  decimal d = decimal::pow10(100);
  cr_assert_eq(d.ilog10(), 100);
  cr_assert(!d.is_inf());
  decimal small = decimal::pow10(-100);
  cr_assert_eq(small.ilog10(), -100);
}

Test(pow10, overflow) {
  cr_assert(decimal::pow10(INT64_MAX).is_inf());
}

Test(pow10, underflow) {
  cr_assert(decimal::pow10(INT64_MIN).is_zero());
  cr_assert(decimal::pow10(-4611686018427387903LL).is_zero());
}

// ---------------------------------------------------------------------------
// ilog10
// ---------------------------------------------------------------------------

Test(ilog10, single_digit) {
  for (int i = 1; i <= 9; i++) {
    decimal d(static_cast<int64_t>(i));
    cr_assert_eq(d.ilog10(), 0, "ilog10(%d) should be 0, got %lld",
                 i, (long long)d.ilog10());
  }
}

Test(ilog10, powers_of_10) {
  cr_assert_eq(decimal::from_string("1").ilog10(), 0);
  cr_assert_eq(decimal::from_string("10").ilog10(), 1);
  cr_assert_eq(decimal::from_string("100").ilog10(), 2);
  cr_assert_eq(decimal::from_string("1000").ilog10(), 3);
  cr_assert_eq(decimal::from_string("1e18").ilog10(), 18);
}

Test(ilog10, just_below_power) {
  cr_assert_eq(decimal::from_string("9").ilog10(), 0);
  cr_assert_eq(decimal::from_string("99").ilog10(), 1);
  cr_assert_eq(decimal::from_string("999").ilog10(), 2);
  cr_assert_eq(decimal::from_string("9999999999999999999").ilog10(), 18);
}

Test(ilog10, fractional) {
  cr_assert_eq(decimal::from_string("0.1").ilog10(), -1);
  cr_assert_eq(decimal::from_string("0.01").ilog10(), -2);
  cr_assert_eq(decimal::from_string("0.001").ilog10(), -3);
  cr_assert_eq(decimal::from_string("0.099").ilog10(), -2);
}

Test(ilog10, negative_values) {
  // ilog10 uses abs, so sign doesn't matter
  cr_assert_eq(decimal::from_string("-100").ilog10(), 2);
  cr_assert_eq(decimal::from_string("-0.01").ilog10(), -2);
}

Test(ilog10, special_and_zero) {
  cr_assert_eq(decimal::nan().ilog10(), 0);
  cr_assert_eq(decimal::inf().ilog10(), 0);
  cr_assert_eq(decimal::zero().ilog10(), 0);
}

// ---------------------------------------------------------------------------
// Optimization regression: make() normalization with table
// ---------------------------------------------------------------------------

Test(make_normalization, round_trip_after_arithmetic) {
  decimal a = decimal::from_string("9999999999999999999");
  decimal b = decimal::from_string("1");
  decimal sum = a + b;
  std::string s = sum.to_string();
  cr_assert_str_eq(s.c_str(), "10000000000000000000");
}

Test(make_normalization, small_mantissa_padding) {
  decimal d = decimal::from_string("1");
  cr_assert_eq(d.mantissa(), 1000000000000000000ULL);
  cr_assert_eq(d.exponent(), -18);
}

Test(make_normalization, uint64_max_from_multiply) {
  decimal a = decimal::from_string("9999999999999999999");
  decimal b = decimal::from_string("9999999999999999999");
  decimal prod = a * b;
  std::string s = prod.to_string();
  cr_assert_str_eq(s.c_str(), "9.999999999999999998e37");
}

Test(make_normalization, add_with_large_exponent_diff) {
  decimal a = decimal::from_string("1e18");
  decimal b = decimal::from_string("1");
  decimal sum = a + b;
  std::string s = sum.to_string();
  cr_assert_str_eq(s.c_str(), "1000000000000000001");
}

Test(make_normalization, sub_with_large_exponent_diff) {
  decimal a = decimal::from_string("1000000000000000001");
  decimal b = decimal::from_string("1");
  decimal diff = a - b;
  std::string s = diff.to_string();
  cr_assert_str_eq(s.c_str(), "1000000000000000000");
}

// ---------------------------------------------------------------------------
// count_digits_u64 correctness — tested indirectly via decimal(uint64_t)
// ---------------------------------------------------------------------------

// For every v in [0, max_exact_uint64], decimal(v).to_string() == to_string(v).
// A wrong digit count would misscale the mantissa and corrupt the string.
Test(count_digits, around_powers_of_ten) {
  static constexpr uint64_t P[20] = {
      1ULL,
      10ULL,
      100ULL,
      1000ULL,
      10000ULL,
      100000ULL,
      1000000ULL,
      10000000ULL,
      100000000ULL,
      1000000000ULL,
      10000000000ULL,
      100000000000ULL,
      1000000000000ULL,
      10000000000000ULL,
      100000000000000ULL,
      1000000000000000ULL,
      10000000000000000ULL,
      100000000000000000ULL,
      1000000000000000000ULL,
      10000000000000000000ULL,
  };

  auto check = [](uint64_t v) {
    std::string expected = std::to_string(v);
    std::string got = decimal(v).to_string();
    cr_assert_str_eq(got.c_str(), expected.c_str(),
                     "decimal(%llu): got \"%s\", want \"%s\"",
                     (unsigned long long)v, got.c_str(), expected.c_str());
  };

  check(0);
  for (int k = 0; k <= 19; k++) {
    uint64_t p = P[k];
    check(p - 1);  // always >= 0: P[0]-1 == 0, which is fine
    if (p <= decimal::max_exact_uint64) check(p);
    if (p < decimal::max_exact_uint64) check(p + 1);
  }
}

Test(max_string_length, never_exceeded) {
  const char* cases[] = {
      "1", "-1", "0.1", "-0.1", "1e3000", "1e-3000",
      "9999999999999999999e3000", "-1234567890123456789e-3000",
      "nan", "inf", "-inf", "0", "-0",
      "1e20", "1e-21", "9.999999999999999999e4000000000000000000",
      "-1.000000000000000001e-4000000000000000000"};
  for (auto s : cases) {
    decimal d = decimal::from_string(s);
    std::string result = d.to_string();
    cr_assert(result.size() <= static_cast<size_t>(decimal::max_string_length),
              "\"%s\" → \"%s\" (%zu chars, max %zu)", s, result.c_str(),
              result.size(), decimal::max_string_length);
  }
}

// ---------------------------------------------------------------------------
// is_special
// ---------------------------------------------------------------------------

Test(is_special, nan_is_special) {
  cr_assert(decimal::nan().is_special());
}

Test(is_special, inf_is_special) {
  cr_assert(decimal::inf().is_special());
  cr_assert(decimal::inf(NEGATIVE).is_special());
}

Test(is_special, finite_is_not_special) {
  cr_assert(!decimal::from_string("1").is_special());
  cr_assert(!decimal::from_string("-42.5").is_special());
  cr_assert(!decimal::from_string("1e3000").is_special());
}

Test(is_special, zero_is_not_special) {
  cr_assert(!decimal::zero().is_special());
  cr_assert(!decimal::zero(NEGATIVE).is_special());
}

// ---------------------------------------------------------------------------
// flip_sign
// ---------------------------------------------------------------------------

Test(flip_sign, positive_to_negative) {
  decimal d = decimal::from_string("5");
  d.flip_sign();
  cr_assert(d.is_negative());
  ASSERT_TOSTR_EQ(d, "-5");
}

Test(flip_sign, negative_to_positive) {
  decimal d = decimal::from_string("-5");
  d.flip_sign();
  cr_assert(!d.is_negative());
  ASSERT_TOSTR_EQ(d, "5");
}

Test(flip_sign, double_flip_is_identity) {
  decimal d = decimal::from_string("3.14");
  decimal orig = d;
  d.flip_sign();
  d.flip_sign();
  cr_assert(d == orig);
}

Test(flip_sign, zero) {
  decimal d = decimal::zero();
  d.flip_sign();
  cr_assert(d.is_zero());
  cr_assert(d.is_negative());

  d.flip_sign();
  cr_assert(d.is_zero());
  cr_assert(!d.is_negative());
}

Test(flip_sign, inf) {
  decimal d = decimal::inf();
  d.flip_sign();
  cr_assert(d.is_inf());
  cr_assert(d.is_negative());
}

Test(flip_sign, nan) {
  decimal d = decimal::nan();
  d.flip_sign();
  cr_assert(d.is_nan());
}

Test(flip_sign, returns_self_reference) {
  decimal d = decimal::from_string("7");
  decimal& ref = d.flip_sign();
  cr_assert(&ref == &d);
  cr_assert(d.is_negative());
}

// ---------------------------------------------------------------------------
// from_double named constructor
// ---------------------------------------------------------------------------

Test(from_double, matches_double_constructor) {
  double vals[] = {0.0, 1.0, -1.0, 3.14, 1e100, -0.0, 1.5};
  for (double v : vals) {
    decimal a(v);
    decimal b = decimal::from_double(v);
    cr_assert(a == b || (a.is_zero() && b.is_zero()),
              "from_double(%g) differs from decimal(%g)", v, v);
    cr_assert_eq(a.mantissa(), b.mantissa());
    cr_assert_eq(a.exponent(), b.exponent());
    cr_assert_eq(a.is_negative(), b.is_negative());
  }
}

Test(from_double, special_values) {
  cr_assert(decimal::from_double(dnan).is_nan());
  cr_assert(decimal::from_double(dinf).is_inf());
  cr_assert(!decimal::from_double(dinf).is_negative());
  cr_assert(decimal::from_double(-dinf).is_inf());
  cr_assert(decimal::from_double(-dinf).is_negative());
}

Test(from_double, rounding_problematic_decimals) {
  // Doubles like 0.1, 0.2, 0.3 can't be represented exactly in binary.
  // Verify round-trip: from_double -> to_double must return the original.
  double vals[] = {0.1, 0.2, 0.3, 0.6, 0.7, 1.1, 2.3, 9.9, 99.99, 0.01};
  for (double v : vals) {
    decimal d(v);
    cr_assert(same_double(d.to_double(), v),
              "from_double(%a) round-trip failed: got %a", v, d.to_double());
    decimal dn(-v);
    cr_assert(same_double(dn.to_double(), -v),
              "from_double(%a) round-trip failed for negated value", v);
  }
}

Test(from_double, subnormals) {
  // Subnormals take the e==0 branch in decompose_double (no implicit leading bit).
  double smallest_subnormal = 5e-324;  // std::numeric_limits<double>::denorm_min()
  cr_assert(std::fpclassify(smallest_subnormal) == FP_SUBNORMAL);

  decimal d = decimal(smallest_subnormal);
  cr_assert(!d.is_zero());
  cr_assert(!d.is_negative());
  cr_assert(same_double(d.to_double(), smallest_subnormal),
            "smallest subnormal round-trip failed");

  // Largest subnormal: just below DBL_MIN.
  double largest_subnormal = std::nextafter(std::numeric_limits<double>::min(), 0.0);
  cr_assert(std::fpclassify(largest_subnormal) == FP_SUBNORMAL);
  decimal d2(largest_subnormal);
  cr_assert(same_double(d2.to_double(), largest_subnormal),
            "largest subnormal round-trip failed");

  // A subnormal in the middle range.
  double mid_subnormal = 1e-310;
  cr_assert(std::fpclassify(mid_subnormal) == FP_SUBNORMAL);
  decimal d3(mid_subnormal);
  cr_assert(same_double(d3.to_double(), mid_subnormal),
            "mid subnormal round-trip failed");

  // Negative subnormal.
  decimal d4(-smallest_subnormal);
  cr_assert(d4.is_negative());
  cr_assert(same_double(d4.to_double(), -smallest_subnormal),
            "negative subnormal round-trip failed");
}

Test(from_double, extremes_of_normal_range) {
  // DBL_MAX stresses the large positive exponent loop (e ~= 971).
  double dmax = std::numeric_limits<double>::max();
  decimal d1(dmax);
  cr_assert(!d1.is_inf());
  cr_assert(!d1.is_zero());
  cr_assert(same_double(d1.to_double(), dmax),
            "DBL_MAX round-trip failed");

  // Negative DBL_MAX.
  decimal d2(-dmax);
  cr_assert(d2.is_negative());
  cr_assert(same_double(d2.to_double(), -dmax),
            "-DBL_MAX round-trip failed");

  // DBL_MIN (smallest positive normal).
  double dmin = std::numeric_limits<double>::min();
  decimal d3(dmin);
  cr_assert(!d3.is_zero());
  cr_assert(same_double(d3.to_double(), dmin),
            "DBL_MIN round-trip failed");

  // Just above DBL_MIN.
  double just_above_min = std::nextafter(dmin, dmax);
  decimal d4(just_above_min);
  cr_assert(same_double(d4.to_double(), just_above_min),
            "nextafter(DBL_MIN, DBL_MAX) round-trip failed");
}

Test(from_double, powers_of_two_at_integer_boundary) {
  // 2^52: smallest power of 2 where all integers up to it are exactly
  // representable. e == 0 in decompose_double for this value.
  double pow2_52 = 4503599627370496.0;  // 2^52
  cr_assert(pow2_52 == std::ldexp(1.0, 52));
  decimal d1(pow2_52);
  cr_assert(same_double(d1.to_double(), pow2_52),
            "2^52 round-trip failed");

  // 2^53: largest exact integer in double.
  double pow2_53 = 9007199254740992.0;  // 2^53
  cr_assert(pow2_53 == std::ldexp(1.0, 53));
  decimal d2(pow2_53);
  cr_assert(same_double(d2.to_double(), pow2_53),
            "2^53 round-trip failed");

  // 2^53 + 1 is NOT representable as double (rounds to 2^53).
  // Verify we at least round-trip the double value.
  double pow2_53_p1 = pow2_53 + 1.0;
  cr_assert(pow2_53_p1 == pow2_53);  // confirm double can't represent it
  decimal d3(pow2_53_p1);
  cr_assert(same_double(d3.to_double(), pow2_53_p1),
            "2^53+1 round-trip failed");

  // 2^53 - 1: largest odd integer representable in double.
  double pow2_53_m1 = 9007199254740991.0;  // 2^53 - 1
  decimal d4(pow2_53_m1);
  cr_assert(same_double(d4.to_double(), pow2_53_m1),
            "2^53-1 round-trip failed");

  // Small powers of 2.
  double pows[] = {1.0, 2.0, 4.0, 8.0, 0.5, 0.25, 0.125};
  for (double v : pows) {
    decimal d(v);
    cr_assert(same_double(d.to_double(), v),
              "power-of-2 %a round-trip failed", v);
  }
}

Test(from_double, wide_range_powers_of_ten) {
  // Exercise many different exponent values across the full double range.
  double vals[] = {
    1e-300, 1e-200, 1e-100, 1e-50, 1e-20, 1e-15, 1e-10, 1e-5, 1e-1,
    1e0, 1e1, 1e5, 1e10, 1e15, 1e20, 1e50, 1e100, 1e200, 1e300,
  };
  for (double v : vals) {
    decimal d(v);
    cr_assert(!d.is_zero());
    cr_assert(!d.is_nan());
    cr_assert(!d.is_inf());
    cr_assert(same_double(d.to_double(), v),
              "1e-power %a round-trip failed", v);
    // Also test negative.
    decimal dn(-v);
    cr_assert(same_double(dn.to_double(), -v),
              "negative 1e-power %a round-trip failed", v);
  }
}

Test(from_double, near_one) {
  // Values very close to 1.0 test precision near a common magnitude.
  double eps = std::numeric_limits<double>::epsilon();  // 2^-52

  double one_plus_eps = 1.0 + eps;
  decimal d1(one_plus_eps);
  cr_assert(same_double(d1.to_double(), one_plus_eps),
            "1+eps round-trip failed");

  double one_minus_half_eps = std::nextafter(1.0, 0.0);
  decimal d2(one_minus_half_eps);
  cr_assert(same_double(d2.to_double(), one_minus_half_eps),
            "nextafter(1,0) round-trip failed");

  double just_above_one = std::nextafter(1.0, 2.0);
  decimal d3(just_above_one);
  cr_assert(same_double(d3.to_double(), just_above_one),
            "nextafter(1,2) round-trip failed");
}

Test(from_double, large_integer_doubles) {
  // Large doubles that are exact integers (e >= 0 path with big shifts).
  double vals[] = {
    std::ldexp(1.0, 60),    // 2^60
    std::ldexp(1.0, 100),   // 2^100
    std::ldexp(1.0, 200),   // 2^200
    std::ldexp(1.0, 500),   // 2^500
    std::ldexp(1.0, 1000),  // 2^1000
    std::ldexp(1.0, 1023),  // 2^1023 (largest power-of-2 double)
  };
  for (double v : vals) {
    cr_assert(std::isfinite(v));
    decimal d(v);
    cr_assert(same_double(d.to_double(), v),
              "2^large %a round-trip failed", v);
  }
}

Test(from_double, tiny_fractions) {
  // Very small positive doubles (e < 0 path with many multiply-by-5 batches).
  double vals[] = {
    std::ldexp(1.0, -100),
    std::ldexp(1.0, -500),
    std::ldexp(1.0, -1000),
    std::ldexp(1.0, -1022),   // smallest normal power-of-2
    std::ldexp(1.0, -1074),   // smallest subnormal (== 5e-324)
  };
  for (double v : vals) {
    cr_assert(v > 0.0);
    decimal d(v);
    cr_assert(!d.is_zero());
    cr_assert(same_double(d.to_double(), v),
              "2^-large %a round-trip failed", v);
  }
}

Test(from_double, nextafter_boundaries) {
  // Test values just above and below key boundaries.
  double boundaries[] = {1.0, 0.5, 2.0, 10.0, 100.0, 1e10, 1e-10};
  for (double b : boundaries) {
    double above = std::nextafter(b, dinf);
    double below = std::nextafter(b, 0.0);
    decimal da(above);
    cr_assert(same_double(da.to_double(), above),
              "nextafter(%a, +inf) round-trip failed", b);
    decimal db(below);
    cr_assert(same_double(db.to_double(), below),
              "nextafter(%a, 0) round-trip failed", b);
  }
}

Test(from_double, round_trip_systematic) {
  // Systematic round-trip test across many magnitudes:
  // for each power of 10 from 1e-308 to 1e+308, test the power itself
  // plus several nextafter steps in each direction.
  for (int exp = -308; exp <= 308; exp++) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "1e%d", exp);
    double base = std::strtod(buf, nullptr);
    if (!std::isfinite(base) || base == 0.0) continue;

    // Test base and a few neighbors.
    double vals[] = {
      base,
      std::nextafter(base, dinf),
      std::nextafter(base, 0.0),
      std::nextafter(std::nextafter(base, dinf), dinf),
    };
    for (double v : vals) {
      if (v == 0.0 || !std::isfinite(v)) continue;
      decimal d(v);
      cr_assert(same_double(d.to_double(), v),
                "systematic round-trip failed for %a (near 1e%d)", v, exp);
      // Also test the negative.
      decimal dn(-v);
      cr_assert(same_double(dn.to_double(), -v),
                "systematic round-trip failed for -%a (near 1e%d)", v, exp);
    }
  }
}

// ---------------------------------------------------------------------------
// std::numeric_limits: remaining members
// ---------------------------------------------------------------------------

Test(numeric_limits, round_error) {
  // round_error() is 0.5 ULP; for decimal that is half the epsilon (5e-19).
  decimal re = std::numeric_limits<decimal>::round_error();
  cr_assert(!re.is_nan());
  cr_assert(!re.is_inf());
  cr_assert(!re.is_zero());
  cr_assert(!re.is_negative());
}

Test(numeric_limits, signaling_nan) {
  // has_signaling_NaN is false; signaling_NaN() returns a quiet NaN.
  cr_assert(!std::numeric_limits<decimal>::has_signaling_NaN);
  decimal snan = std::numeric_limits<decimal>::signaling_NaN();
  cr_assert(snan.is_nan());
}

Test(numeric_limits, denorm_min) {
  // has_denorm is denorm_absent; denorm_min() returns the same as min().
  decimal dm = std::numeric_limits<decimal>::denorm_min();
  decimal m = std::numeric_limits<decimal>::min();
  cr_assert(dm == m);
}

// ---------------------------------------------------------------------------
// Coverage: add_abs internal swap (decimals.cc lines 276-278)
//
// add_abs swaps its operands when the first has a smaller biased exponent than
// the second.  All existing addition tests pass the larger-exponent value
// first.  Reversing the order triggers the swap.
// ---------------------------------------------------------------------------

Test(addition, smaller_exponent_first) {
  // "0.001" exp=-21, "1000" exp=-15: ea < eb, so add_abs swaps them.
  decimal a = decimal::from_string("0.001");
  decimal b = decimal::from_string("1000");
  ASSERT_TOSTR_EQ((a + b), "1000.001");

  // Same result regardless of order (commutativity already tested, but this
  // specifically exercises the swap branch).
  ASSERT_TOSTR_EQ((b + a), "1000.001");

  // Negative operands — same sign, so add_abs is called; swap still fires.
  ASSERT_TOSTR_EQ((-a + -b), "-1000.001");
}

// ---------------------------------------------------------------------------
// Coverage: operator+ line 726-727 (bea > beb, different signs)
//
// When *this has a strictly larger biased exponent than rhs and the signs
// differ, operator+ calls sub_magnitudes(*this, rhs, sa).  Existing
// different-sign tests ("3"+"-5", "7.7"+"-7.7") use values with equal
// biased exponents.  "1000" has biased_exp=-15, "5" has biased_exp=-18,
// so bea > beb and line 726 fires.
// ---------------------------------------------------------------------------

Test(addition, different_sign_lhs_larger_exponent) {
  // bea=-15 > beb=-18 → sub_magnitudes(1000, 5, POSITIVE) → 995.
  decimal a = decimal::from_string("1000");
  decimal b = decimal::from_string("-5");
  ASSERT_TOSTR_EQ((a + b), "995");

  cr_assert_eq(1000.0 + (-5.0), 995.0);
}

// ---------------------------------------------------------------------------
// Coverage: operator+ line 731 (bea == beb, ma > mb, different signs)
//
// When biased exponents are equal and the positive mantissa exceeds the
// negative mantissa, execution falls through to the last sub_magnitudes
// call.  "3"+"-5" has mb > ma; "7.7"+"-7.7" has ma == mb.  "5"+"-3" is
// the missing case: bea == beb == -18, ma(5) > mb(3).
// ---------------------------------------------------------------------------

Test(addition, different_sign_lhs_larger_mantissa) {
  // bea==beb==-18, ma=5e18 > mb=3e18 → sub_magnitudes(5, 3, POSITIVE) → 2.
  decimal a = decimal::from_string("5");
  decimal b = decimal::from_string("-3");
  ASSERT_TOSTR_EQ((a + b), "2");

  cr_assert_eq(5.0 + (-3.0), 2.0);
}

// ---------------------------------------------------------------------------
// Coverage: add_magnitudes line 262 (diff >= 20, same sign)
//
// When the biased-exponent gap between the two same-sign operands is >= 20,
// the smaller value rounds away completely and add_magnitudes returns the
// larger operand unchanged.  biased_exp("1e20")=2, biased_exp("1")=-18:
// diff = 20, exactly triggering the early return.
// ---------------------------------------------------------------------------

Test(addition, huge_exponent_diff_same_sign) {
  decimal a = decimal::from_string("1e20");
  decimal b = decimal::from_string("1");
  ASSERT_TOSTR_EQ((a + b), "1e20");

  // Reverse order exercises the internal swap (ea < eb path) before the
  // diff >= 20 early return.
  ASSERT_TOSTR_EQ((b + a), "1e20");
}

// ---------------------------------------------------------------------------
// Coverage: sub_magnitudes line 285 (diff >= 20, different signs)
//
// Same exponent gap as above, but with opposite signs: operator+ dispatches
// through the bea > beb branch (line 726-727) into sub_magnitudes, which
// hits its own diff >= 20 early return.
// ---------------------------------------------------------------------------

Test(addition, huge_exponent_diff_different_sign) {
  decimal a = decimal::from_string("1e20");
  decimal b = decimal::from_string("-1");
  ASSERT_TOSTR_EQ((a + b), "1e20");
}

// ---------------------------------------------------------------------------
// Coverage: operator uint64_t exp==1 non-overflowing path (decimals.cc line 500)
//
// When biased_exponent==1 the cast computes m*10.  The guard at line 499
// (returning UINT64_MAX on overflow) is already hit by the existing large-
// value tests.  We need a value whose mantissa fits without overflow so that
// execution reaches the "return m * 10" on line 500.
//
// 1.1e19 = mantissa 1100000000000000000, biased_exp 1.
// 1100000000000000000 <= UINT64_MAX/10 (1844674407370955161), so no overflow.
// ---------------------------------------------------------------------------

Test(operator_uint64, exp_one_no_overflow) {
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("1.1e19")),
               11000000000000000000ULL);
  // Also check the boundary: just below the overflow threshold.
  // 1.844674407370955161e19 rounds to mantissa 1844674407370955161, exp 1.
  // m*10 = 18446744073709551610 which fits in uint64_t.
  cr_assert_eq(static_cast<uint64_t>(decimal::from_string("1.844674407370955161e19")),
               18446744073709551610ULL);
}

// ---------------------------------------------------------------------------
// Coverage: from_string hex-float strtod-failure path (decimals.cc lines 623-624)
//
// When the input looks like a hex float ("0x"/"0X" prefix) but strtod makes
// no progress (e.g. bare "0x" with no digits), from_string returns zero and
// sets *endptr to the original string pointer.
// The test mirrors the style of hex_float_no_p_exponent: verify that
// decimal's result and endptr consumption exactly match what strtod does.
// ---------------------------------------------------------------------------

Test(from_string, hex_float_no_significand) {
  const char* input = "0x";
  char* dend = nullptr;
  double dv = std::strtod(input, &dend);

  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);

  // decimal must match strtod: same value, same number of characters consumed.
  cr_assert(same_double(d.to_double(), dv),
            "value mismatch: decimal=%s strtod=%g", d.to_string().c_str(), dv);
  cr_assert_eq(end - input, dend - input,
               "endptr mismatch: decimal consumed %td, strtod consumed %td",
               end - input, dend - input);
}

// When 'p'/'P' is present but followed by a non-digit (e.g. "0x1pZ"),
// the exponent is invalid.  strtod treats the 'p' as not consumed and
// falls back; decimal must match.
Test(from_string, hex_float_invalid_p_exponent) {
  const char* cases[] = {
    // bare p, no exponent digits
    "0x1p", "0x1P",
    // sign but no digits
    "0x1p+", "0x1p-", "0x1P+", "0x1P-",
    // sign then non-digit
    "0x1p+Z", "0x1p-Z", "0x1p+.", "0x1p-!",
    // non-digit directly after p: letters, punctuation, high bytes
    "0x1pZ", "0x1pa", "0x1pG", "0x1p!", "0x1p.", "0x1p ", "0x1p\t",
    "0x1p@", "0x1p[", "0x1p/", "0x1p:",
    // with fractional significand
    "0x1.8pZ", "0x1.8p+Z", "0x1.8p",
    // uppercase P
    "0x1.8PZ", "0x1.8P+Z", "0x1.8P",
  };
  for (auto input : cases) {
    char* dend = nullptr;
    double dv = std::strtod(input, &dend);

    const char* end = nullptr;
    decimal d = decimal::from_string(input, &end);

    cr_assert(same_double(d.to_double(), dv),
              "value mismatch for \"%s\": decimal=%s strtod=%g",
              input, d.to_string().c_str(), dv);
    cr_assert_eq(end - input, dend - input,
                 "endptr mismatch for \"%s\": decimal consumed %td, strtod consumed %td",
                 input, end - input, dend - input);
  }
}

// Verify all 256 byte values after 'p': only '0'-'9' (and +/- followed by
// '0'-'9') should be accepted as valid exponent starts.
Test(from_string, hex_float_p_exponent_all_bytes) {
  char buf[8]; // "0x1p" + one byte + NUL
  for (int c = 1; c < 256; ++c) {
    if (c >= '0' && c <= '9') continue; // skip valid digits
    snprintf(buf, sizeof(buf), "0x1p%c", c);

    char* dend = nullptr;
    double dv = std::strtod(buf, &dend);

    const char* end = nullptr;
    decimal d = decimal::from_string(buf, &end);

    cr_assert(same_double(d.to_double(), dv),
              "value mismatch for byte %d (0x1p%c): decimal=%s strtod=%g",
              c, c, d.to_string().c_str(), dv);
    cr_assert_eq(end - buf, dend - buf,
                 "endptr mismatch for byte %d (0x1p%c): decimal consumed %td, strtod consumed %td",
                 c, c, end - buf, dend - buf);
  }
}

// Verify that the exponent loop stops exactly at non-digit characters.
// Tests multi-digit exponents followed by every possible trailing byte,
// ensuring the parser doesn't over-consume (e.g. by comparing against '9'
// instead of 9 in the loop condition).
Test(from_string, hex_float_p_exponent_loop_termination) {
  char buf[16];
  for (int c = 1; c < 256; ++c) {
    if (c >= '0' && c <= '9') continue;
    // "0x1p10" + trailing byte: the exponent is 10, and c must not be consumed.
    snprintf(buf, sizeof(buf), "0x1p10%c", c);

    char* dend = nullptr;
    double dv = std::strtod(buf, &dend);

    const char* end = nullptr;
    decimal d = decimal::from_string(buf, &end);

    cr_assert(same_double(d.to_double(), dv),
              "value mismatch for trailing byte %d after 0x1p10: decimal=%s strtod=%g",
              c, d.to_string().c_str(), dv);
    cr_assert_eq(end - buf, dend - buf,
                 "endptr mismatch for trailing byte %d after 0x1p10: decimal consumed %td, strtod consumed %td",
                 c, end - buf, dend - buf);
  }
}

// Regression: when input is "0x." (dot but no digits), parse_hex_float used
// q-1 as endptr after consuming the dot, yielding p+2 instead of p+1.
// strtod("0x.", ...) consumes only "0", so endptr must equal input+1.
Test(from_string, hex_float_no_significand_with_dot) {
  const char* input = "0x.";
  char* dend = nullptr;
  double dv = std::strtod(input, &dend);

  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);

  cr_assert(same_double(d.to_double(), dv),
            "value mismatch: decimal=%s strtod=%g", d.to_string().c_str(), dv);
  cr_assert_eq(end - input, dend - input,
               "endptr mismatch: decimal consumed %td, strtod consumed %td",
               end - input, dend - input);
}

// ---------------------------------------------------------------------------
// to_double: thorough tests for string-constructed decimals
//
// These test to_double with values that did NOT originate from a double,
// exercising the integer-arithmetic conversion path directly.  Each case
// is verified against strtod (the gold standard for decimal→double).
// ---------------------------------------------------------------------------

// Helper: verify decimal::from_string(s).to_double() == strtod(s, nullptr).
static void check_to_double_vs_strtod(const char* s) {
  decimal d = decimal::from_string(s);
  double got = d.to_double();
  double expected = std::strtod(s, nullptr);
  cr_assert(same_double(got, expected),
            "to_double mismatch for \"%s\": got %a, expected %a", s, got, expected);
}

Test(to_double, exact_integers) {
  const char* cases[] = {
      "1", "2", "10", "100", "1000000", "999999999999999",
      "9007199254740992",   // 2^53
      "9007199254740993",   // 2^53 + 1 (not exactly representable)
      "18014398509481984",  // 2^54
      "9999999999999999999", // 19 nines (max mantissa)
  };
  for (auto s : cases) check_to_double_vs_strtod(s);
}

Test(to_double, exact_negative_integers) {
  const char* cases[] = {"-1", "-10", "-9007199254740992", "-9999999999999999999"};
  for (auto s : cases) check_to_double_vs_strtod(s);
}

Test(to_double, simple_fractions) {
  // Fractions that can't be exactly represented in binary.
  const char* cases[] = {
      "0.1", "0.2", "0.3", "0.7", "0.9",
      "0.01", "0.001", "0.123456789",
      "3.14159265358979323",  // full 19-digit pi
      "2.7182818284590452",   // e
      "0.1111111111111111111", // 19 ones after dot
  };
  for (auto s : cases) check_to_double_vs_strtod(s);
}

Test(to_double, negative_fractions) {
  const char* cases[] = {"-0.1", "-0.5", "-3.14159265358979323"};
  for (auto s : cases) check_to_double_vs_strtod(s);
}

Test(to_double, powers_of_ten) {
  // Sweep 10^e for e in [-323, 308], verified against strtod.
  for (int e = -323; e <= 308; e++) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "1e%d", e);
    check_to_double_vs_strtod(buf);
  }
}

Test(to_double, large_mantissa_times_power_of_ten) {
  // 19-digit mantissa with varying exponents — stresses both multiplication
  // and division paths with maximum-size mantissa.
  const char* mantissas[] = {
      "9999999999999999999",
      "1000000000000000000",
      "5000000000000000000",
      "1234567890123456789",
  };
  int exponents[] = {-340, -308, -200, -100, -50, -18, -1, 0, 1, 18, 50, 100, 200, 289};
  for (auto m : mantissas) {
    for (int e : exponents) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%se%d", m, e);
      double expected = std::strtod(buf, nullptr);
      if (!std::isfinite(expected) || expected == 0.0) continue;
      check_to_double_vs_strtod(buf);
    }
  }
}

Test(to_double, near_double_overflow) {
  // Values near DBL_MAX ≈ 1.7976931348623157e308.
  const char* cases[] = {
      "1.7976931348623157e308",  // DBL_MAX
      "1.7976931348623158e308",  // just above DBL_MAX → inf
      "1.7976931348623156e308",  // just below DBL_MAX
      "1e308",
      "9.999e307",
      "1.8e308",                 // overflow → inf
      "2e308",                   // overflow → inf
  };
  for (auto s : cases) check_to_double_vs_strtod(s);
}

Test(to_double, near_double_underflow) {
  // Values near the smallest subnormal (5e-324) and smallest normal (2.2e-308).
  const char* cases[] = {
      "5e-324",                  // smallest subnormal
      "4e-324",                  // rounds to 0 or subnormal
      "1e-323",
      "2.2250738585072014e-308", // DBL_MIN (smallest normal)
      "2.2250738585072013e-308", // just below DBL_MIN (subnormal)
      "1e-310",                  // subnormal range
      "1e-320",                  // deep subnormal
      "1e-324",                  // at the edge
      "1e-325",                  // underflow to 0
  };
  for (auto s : cases) check_to_double_vs_strtod(s);
}

Test(to_double, overflow_returns_inf) {
  decimal d = decimal::from_string("1e310");
  cr_assert(std::isinf(d.to_double()));
  cr_assert(d.to_double() > 0);

  decimal dn = decimal::from_string("-1e310");
  cr_assert(std::isinf(dn.to_double()));
  cr_assert(dn.to_double() < 0);
}

Test(to_double, underflow_returns_zero) {
  decimal d = decimal::from_string("1e-400");
  cr_assert_eq(d.to_double(), 0.0);
  cr_assert(!std::signbit(d.to_double()));

  decimal dn = decimal::from_string("-1e-400");
  cr_assert_eq(dn.to_double(), 0.0);
  cr_assert(std::signbit(dn.to_double()));
}

Test(to_double, zero_and_negative_zero) {
  cr_assert_eq(decimal::zero(POSITIVE).to_double(), 0.0);
  cr_assert(!std::signbit(decimal::zero(POSITIVE).to_double()));
  cr_assert_eq(decimal::zero(NEGATIVE).to_double(), 0.0);
  cr_assert(std::signbit(decimal::zero(NEGATIVE).to_double()));
}

Test(to_double, specials) {
  cr_assert(std::isnan(decimal::nan().to_double()));
  cr_assert(std::isinf(decimal::inf(POSITIVE).to_double()));
  cr_assert(decimal::inf(POSITIVE).to_double() > 0);
  cr_assert(std::isinf(decimal::inf(NEGATIVE).to_double()));
  cr_assert(decimal::inf(NEGATIVE).to_double() < 0);
}

Test(to_double, exp10_near_early_exit_thresholds) {
  // exp10 just inside/outside the early-exit bounds (309 and -363).
  const char* overflow_cases[] = {"1e309", "1e310", "1e350", "1e1000"};
  for (auto s : overflow_cases) {
    decimal d = decimal::from_string(s);
    cr_assert(std::isinf(d.to_double()),
              "\"%s\" should overflow to inf", s);
  }
  const char* underflow_cases[] = {"1e-350", "1e-363", "1e-364", "1e-1000"};
  for (auto s : underflow_cases) {
    decimal d = decimal::from_string(s);
    cr_assert_eq(d.to_double(), 0.0,
                 "\"%s\" should underflow to zero", s);
  }
}

Test(to_double, many_loop_iterations) {
  // Large positive exponents force many iterations of the 5^n multiply loop.
  // exp10 = 290 → 290/27 ≈ 11 iterations.
  check_to_double_vs_strtod("1e290");
  check_to_double_vs_strtod("1e289");
  check_to_double_vs_strtod("9.999999999999999999e289");

  // Large negative exponents force many iterations of the 5^n divide loop.
  // exp10 = -340 → 340/27 ≈ 13 iterations.
  check_to_double_vs_strtod("1e-340");
  check_to_double_vs_strtod("1e-323");
  check_to_double_vs_strtod("9.999999999999999999e-324");
}

Test(to_double, half_ulp_rounding_boundaries) {
  // Values exactly at rounding boundaries for double.
  // 2^53 + 0.5 should round to 2^53 (round-to-even).
  check_to_double_vs_strtod("9007199254740992.5");
  // 2^53 + 1.5 should round to 2^53 + 2.
  check_to_double_vs_strtod("9007199254740993.5");
  // A value that exercises rounding in the subnormal range.
  check_to_double_vs_strtod("7.4e-324");
  check_to_double_vs_strtod("2.5e-324");
}

Test(to_double, from_arithmetic_results) {
  // Values produced by decimal arithmetic, not from strings or doubles.
  decimal a = decimal::from_string("1") / decimal::from_string("3");
  double expected = std::strtod(a.to_string().c_str(), nullptr);
  cr_assert(same_double(a.to_double(), expected),
            "1/3 to_double mismatch: got %a, expected %a", a.to_double(), expected);

  decimal b = decimal::from_string("1") / decimal::from_string("7");
  expected = std::strtod(b.to_string().c_str(), nullptr);
  cr_assert(same_double(b.to_double(), expected),
            "1/7 to_double mismatch: got %a, expected %a", b.to_double(), expected);

  decimal c = decimal::from_string("1e200") * decimal::from_string("1e100");
  expected = std::strtod(c.to_string().c_str(), nullptr);
  cr_assert(same_double(c.to_double(), expected),
            "1e300 to_double mismatch");
}

Test(to_double, systematic_sweep) {
  // Sweep many mantissa/exponent combinations against strtod.
  uint64_t mantissas[] = {
      1000000000000000000ULL,  // 10^18 (minimum normalized)
      1000000000000000001ULL,  // 10^18 + 1
      5000000000000000000ULL,  // midpoint
      9999999999999999999ULL,  // maximum
      1844674407370955161ULL,  // near 2^60.67 (arbitrary)
  };
  for (uint64_t m : mantissas) {
    for (int e = -342; e <= 290; e += 7) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%llue%d",
                    static_cast<unsigned long long>(m), e);
      double expected = std::strtod(buf, nullptr);
      if (!std::isfinite(expected)) continue;
      decimal d = decimal::from_string(buf);
      cr_assert(same_double(d.to_double(), expected),
                "sweep mismatch for %s: got %a, expected %a",
                buf, d.to_double(), expected);
    }
  }
}

// ---------------------------------------------------------------------------
// Bug investigation: from_string endptr edge cases
// ---------------------------------------------------------------------------

// Suspected bug #1: bare "." should be rejected (no digits at all).
// strtod(".", &end) sets end to the original string (no conversion).
// from_string should do the same.
Test(bug_investigation, bare_dot_rejected) {
  // Verify strtod behavior first.
  const char* dinput = ".";
  char* dend = nullptr;
  double dv = std::strtod(dinput, &dend);
  cr_assert_eq(dv, 0.0);
  cr_assert_eq(dend, dinput, "strtod should reject bare dot");

  // Now test decimal::from_string.
  const char* input = ".";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_zero());
  cr_assert_eq(end, input,
               "from_string(\".\") should reject bare dot (endptr should equal input), "
               "but endptr is %td chars past input", end - input);
}

// Suspected bug #2: exponent not consumed for zero-valued numbers.
// strtod("0e5", &end) fully consumes all 3 characters.
// from_string should do the same.
Test(bug_investigation, zero_exponent_consumed) {
  // Verify strtod behavior first.
  const char* dinput = "0e5";
  char* dend = nullptr;
  double dv = std::strtod(dinput, &dend);
  cr_assert_eq(dv, 0.0);
  cr_assert_eq(dend - dinput, 3, "strtod should consume all of \"0e5\"");

  // Now test decimal::from_string.
  const char* input = "0e5";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_zero());
  cr_assert_eq(end - input, 3,
               "from_string(\"0e5\") should consume all 3 chars, "
               "but consumed only %td", end - input);
}

// Same bug, variant: "0.0e5" — fractional zero with exponent.
Test(bug_investigation, zero_fraction_exponent_consumed) {
  const char* dinput = "0.0e5";
  char* dend = nullptr;
  double dv = std::strtod(dinput, &dend);
  cr_assert_eq(dv, 0.0);
  cr_assert_eq(dend - dinput, 5, "strtod should consume all of \"0.0e5\"");

  const char* input = "0.0e5";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_zero());
  cr_assert_eq(end - input, 5,
               "from_string(\"0.0e5\") should consume all 5 chars, "
               "but consumed only %td", end - input);
}

// Same bug, variant: "0.0e-99" — negative exponent on zero.
Test(bug_investigation, zero_negative_exponent_consumed) {
  const char* dinput = "0.0e-99";
  char* dend = nullptr;
  double dv = std::strtod(dinput, &dend);
  cr_assert_eq(dv, 0.0);
  cr_assert_eq(dend - dinput, 7, "strtod should consume all of \"0.0e-99\"");

  const char* input = "0.0e-99";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_zero());
  cr_assert_eq(end - input, 7,
               "from_string(\"0.0e-99\") should consume all 7 chars, "
               "but consumed only %td", end - input);
}

// When the mantissa is zero and 'e' is NOT followed by a digit, strtod
// backtracks past the 'e' (and any sign character).  from_string should
// do the same.  We test every combination of:
//   zero mantissa: "0", "00", "0.0", ".0", "00.00"
//   bare 'e', 'e+', 'e-'
// followed by a non-digit character ('z') so we can verify the stop position.

#define ZERO_E_NO_DIGIT_TEST(name, literal)                                    \
  Test(bug_investigation, zero_e_no_digit_##name) {                            \
    const char* dinput = literal;                                              \
    char* dend = nullptr;                                                      \
    std::strtod(dinput, &dend);                                                \
    ptrdiff_t strtod_len = dend - dinput;                                      \
                                                                               \
    const char* input = literal;                                               \
    const char* end = nullptr;                                                 \
    decimal d = decimal::from_string(input, &end);                             \
    cr_assert(d.is_zero(),                                                     \
              "from_string(\"%s\") should be zero", literal);                  \
    cr_assert_eq(end - input, strtod_len,                                      \
                 "from_string(\"%s\") consumed %td chars, "                    \
                 "strtod consumed %td",                                        \
                 literal, end - input, strtod_len);                            \
  }

// "0e" followed by non-digit
ZERO_E_NO_DIGIT_TEST(0ez,       "0ez")
ZERO_E_NO_DIGIT_TEST(0eplus_z,  "0e+z")
ZERO_E_NO_DIGIT_TEST(0eminus_z, "0e-z")

// "00e" variants
ZERO_E_NO_DIGIT_TEST(00ez,       "00ez")
ZERO_E_NO_DIGIT_TEST(00eplus_z,  "00e+z")
ZERO_E_NO_DIGIT_TEST(00eminus_z, "00e-z")

// "0.0e" variants
ZERO_E_NO_DIGIT_TEST(0_dot_0ez,       "0.0ez")
ZERO_E_NO_DIGIT_TEST(0_dot_0eplus_z,  "0.0e+z")
ZERO_E_NO_DIGIT_TEST(0_dot_0eminus_z, "0.0e-z")

// ".0e" variants
ZERO_E_NO_DIGIT_TEST(dot_0ez,       ".0ez")
ZERO_E_NO_DIGIT_TEST(dot_0eplus_z,  ".0e+z")
ZERO_E_NO_DIGIT_TEST(dot_0eminus_z, ".0e-z")

// "00.00e" variants
ZERO_E_NO_DIGIT_TEST(00_dot_00ez,       "00.00ez")
ZERO_E_NO_DIGIT_TEST(00_dot_00eplus_z,  "00.00e+z")
ZERO_E_NO_DIGIT_TEST(00_dot_00eminus_z, "00.00e-z")

// Edge: 'e' at end of string (no trailing char at all)
ZERO_E_NO_DIGIT_TEST(0e_eos,       "0e")
ZERO_E_NO_DIGIT_TEST(0eplus_eos,   "0e+")
ZERO_E_NO_DIGIT_TEST(0eminus_eos,  "0e-")
ZERO_E_NO_DIGIT_TEST(0_dot_0e_eos, "0.0e")

// ---------------------------------------------------------------------------
// Bug investigation: is_negative() contract for NaN
//
// The header documents: is_negative() // false for NaN
// But the implementation just checks the sign bit, so -nan() or
// from_string("-nan") will return true.
// ---------------------------------------------------------------------------

Test(bug_investigation, is_negative_matches_signbit_for_nan) {
  // is_negative() behaves like std::signbit: reports the raw sign bit,
  // even for NaN.  Verify it matches double's std::signbit behavior.

  cr_assert(!decimal::nan().is_negative(),
            "nan().is_negative() should be false (sign bit not set)");
  cr_assert(!std::signbit(dnan));

  decimal neg_nan = -decimal::nan();
  cr_assert(neg_nan.is_nan(), "-nan should still be NaN");
  cr_assert(neg_nan.is_negative(),
            "(-nan).is_negative() should be true (sign bit is set)");
  cr_assert(std::signbit(-dnan));
}

// ---------------------------------------------------------------------------
// Bug investigation: from_string sign of zero
//
// Parsing "-0e5" should produce negative zero (sign preserved through
// the exponent-consuming code path for zero-mantissa values).
// ---------------------------------------------------------------------------

Test(bug_investigation, negative_zero_with_exponent_preserves_sign) {
  // strtod("-0e5") produces -0.0
  char* dend = nullptr;
  double dv = std::strtod("-0e5", &dend);
  cr_assert_eq(dv, 0.0);
  cr_assert(std::signbit(dv), "strtod(\"-0e5\") should be negative zero");

  decimal d = decimal::from_string("-0e5");
  cr_assert(d.is_zero(), "\"-0e5\" should be zero");
  cr_assert(d.is_negative(),
            "\"-0e5\" should be negative zero (sign should be preserved)");
}

Test(bug_investigation, negative_zero_fraction_with_exponent_preserves_sign) {
  decimal d = decimal::from_string("-0.0e10");
  cr_assert(d.is_zero(), "\"-0.0e10\" should be zero");
  cr_assert(d.is_negative(),
            "\"-0.0e10\" should be negative zero");
}

// ---------------------------------------------------------------------------
// Bug investigation: operator uint64_t with exp == 1 overflow boundary
//
// When mantissa * 10 would overflow uint64_t, the code should saturate to
// UINT64_MAX. Test the exact boundary: m = UINT64_MAX/10 + 1.
// ---------------------------------------------------------------------------

Test(bug_investigation, uint64_cast_exp1_exact_overflow_boundary) {
  // UINT64_MAX / 10 = 1844674407370955161
  // m = 1844674407370955162 with exp=1 → m*10 overflows
  // The value "1.844674407370955162e19" has mantissa 1844674407370955162.
  decimal d = decimal::from_string("1.844674407370955162e19");
  // This should saturate to UINT64_MAX, not wrap around.
  uint64_t result = static_cast<uint64_t>(d);
  cr_assert_eq(result, UINT64_MAX,
               "uint64 cast of 1.844674407370955162e19 should saturate to UINT64_MAX");
}

// ---------------------------------------------------------------------------
// Bug investigation: division rounding at exact midpoints
//
// Verify that the rounding in division ((scaled % mb) * 2 >= mb) uses the
// correct comparison. With exact halfway values the result should round up.
// ---------------------------------------------------------------------------

Test(bug_investigation, division_exact_midpoint_rounding) {
  // 1 / 6 = 0.1666...  In 19 digits: 1666666666666666667 (rounds up)
  decimal result = decimal::from_string("1") / decimal::from_string("6");
  // The last digit should be 7 (round up from ...6.666...), not 6.
  std::string s = result.to_string();
  cr_assert_str_eq(s.c_str(), "0.1666666666666666667",
                   "1/6 should round up the last digit to 7, got %s", s.c_str());
}

// ---------------------------------------------------------------------------
// Bug investigation: parse_mantissa loop termination on NUL
//
// The parse_mantissa loop checks `while (*p)` but the dropping=2 path
// increments extra_digits for every character. If a NUL-terminated string
// has exactly 19 digits before a dot, the 20th digit rounding should work
// correctly at the string boundary.
// ---------------------------------------------------------------------------

Test(bug_investigation, parse_mantissa_exact_19_digits_at_nul) {
  // Exactly 19 digits, no dot, no exponent, no trailing chars
  ASSERT_TOSTR_EQ(decimal::from_string("1000000000000000001"), "1000000000000000001");
  // 20 digits: the 20th digit triggers rounding, then hits NUL
  ASSERT_TOSTR_EQ(decimal::from_string("10000000000000000015"), "10000000000000000020");
}

// ---------------------------------------------------------------------------
// Bug investigation: add_impl slow path returns a (NaN) directly
//
// When a is NaN and b is inf, add_impl returns `a` unchanged (line 334).
// This preserves the NaN's sign bit. Verify that NaN propagation doesn't
// accidentally carry a sign through arithmetic.
// ---------------------------------------------------------------------------

Test(bug_investigation, nan_plus_inf_returns_nan) {
  decimal result = decimal::nan() + decimal::inf(NEGATIVE);
  cr_assert(result.is_nan(), "nan + (-inf) should be nan");

  decimal result2 = decimal::inf(NEGATIVE) + decimal::nan();
  cr_assert(result2.is_nan(), "(-inf) + nan should be nan");
}

// ---------------------------------------------------------------------------
// Bug investigation: comparison of values with same exponent but different
// mantissa, both negative.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Coverage: inf with endptr (decimals.cc lines 896-901)
//
// When endptr is non-null, from_string must consume "inf" (3 chars) or
// "infinity" (8 chars) and set *endptr past the consumed text.
// All existing inf tests either pass endptr=nullptr or use from_string(string).
// ---------------------------------------------------------------------------

Test(coverage, inf_endptr_short) {
  const char* input = "inf";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_inf());
  cr_assert(!d.is_negative());
  cr_assert_eq(end - input, 3);
}

Test(coverage, inf_endptr_full_infinity) {
  const char* input = "infinity";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_inf());
  cr_assert(!d.is_negative());
  cr_assert_eq(end - input, 8);
}

Test(coverage, inf_endptr_trailing) {
  const char* input = "infxyz";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_inf());
  cr_assert_eq(end - input, 3);
  cr_assert_eq(*end, 'x');
}

Test(coverage, inf_endptr_negative) {
  const char* input = "-infinity";
  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);
  cr_assert(d.is_inf());
  cr_assert(d.is_negative());
  cr_assert_eq(end - input, 9);
}

// ---------------------------------------------------------------------------
// Coverage: hex float with 16+ significant hex digits (decimals.cc lines 384, 389)
//
// The hex mantissa parser switches to "overflowing" mode after 16 significant
// hex digits (sig_count == 16). After that, it accumulates bit_adj += 4 for
// each extra digit. Need a hex float with 17+ hex digits.
// ---------------------------------------------------------------------------

Test(coverage, hex_float_overflow_many_digits) {
  // 17+ hex digits: after 16 significant hex digits the parser switches to
  // overflowing mode. Use hex letters (a-f) after the 16th digit to cover
  // the nib>9 branch inside the overflowing template (line 384).
  const char* input = "0x1234567890abcdefabcp0";
  char* dend = nullptr;
  double dv = std::strtod(input, &dend);

  const char* end = nullptr;
  decimal d = decimal::from_string(input, &end);

  cr_assert(same_double(d.to_double(), dv),
            "hex overflow: value mismatch: decimal=%s strtod=%g",
            d.to_string().c_str(), dv);
  cr_assert_eq(end - input, dend - input,
               "hex overflow: endptr mismatch");
}

// ---------------------------------------------------------------------------
// Coverage: to_double quotient fits in 64 bits (decimals.cc line 697)
//
// The negative-exponent division loop in to_double computes
// quotient = (m << shift) / POW5[batch]. When qhi == 0, execution takes the
// else branch (line 697). This happens when m is small enough that the
// quotient fits in 64 bits after dividing by a large power of 5.
// ---------------------------------------------------------------------------

Test(coverage, to_double_quotient_fits_64bit) {
  // A value with a small mantissa and a small negative exponent.
  // "1e-1" → mantissa 1000000000000000000, exp10 = -19
  // remaining = 19; batch = 19; POW5[19] = 5^19 = 19073486328125
  // shift = clz(m) + 64; quotient = (m << shift) / 5^19
  // For small batches, qhi is often 0 if m is not too large.
  // Let's try a value that triggers this.
  check_to_double_vs_strtod("1e-1");
  check_to_double_vs_strtod("1e-2");
  check_to_double_vs_strtod("1e-3");
}

Test(bug_investigation, negative_same_exponent_comparison) {
  // -1.5 and -1.2 have the same exponent, but -1.5 < -1.2.
  decimal a = decimal::from_string("-1.5");
  decimal b = decimal::from_string("-1.2");
  cr_assert(a < b, "-1.5 should be less than -1.2");
  cr_assert(!(b < a), "-1.2 should not be less than -1.5");
  cr_assert(b > a);
  cr_assert(a <= b);
  cr_assert(b >= a);
  cr_assert(a != b);
}
