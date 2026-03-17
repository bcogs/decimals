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

// ---------------------------------------------------------------------------
// Construction & queries
// ---------------------------------------------------------------------------

Test(construction, default_is_zero) {
  decimal d;
  cr_assert(d.is_zero());
  cr_assert(!d.is_nan());
  cr_assert(!d.is_inf());
  cr_assert(d.is_finite());
  cr_assert(!d.is_negative());
  cr_assert_eq(d.mantissa(), 0u);
}

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
  cr_assert_str_eq(d.to_string().c_str(), "1.5");
}

Test(construction, from_double_negative) {
  decimal d(-42.0);
  cr_assert(d.is_negative());
  cr_assert_str_eq(d.to_string().c_str(), "-42");
}

Test(construction, from_int64_positive) {
  decimal d(static_cast<int64_t>(42));
  cr_assert_str_eq(d.to_string().c_str(), "42");
  cr_assert(!d.is_negative());
}

Test(construction, from_int64_negative) {
  decimal d(static_cast<int64_t>(-42));
  cr_assert_str_eq(d.to_string().c_str(), "-42");
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
  // UINT64_MAX = 18446744073709551615, which has 20 digits → rounded to 19.
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
  cr_assert_str_eq(d.to_string().c_str(), "12345");
}

Test(from_string, decimal) {
  decimal d = decimal::from_string("123.456");
  cr_assert_str_eq(d.to_string().c_str(), "123.456");
}

Test(from_string, leading_zeros) {
  decimal d = decimal::from_string("007.5");
  cr_assert_str_eq(d.to_string().c_str(), "7.5");
}

Test(from_string, leading_dot) {
  decimal d = decimal::from_string(".25");
  cr_assert_str_eq(d.to_string().c_str(), "0.25");
}

Test(from_string, trailing_dot) {
  decimal d = decimal::from_string("42.");
  cr_assert_str_eq(d.to_string().c_str(), "42");
}

Test(from_string, zero_point_something) {
  decimal d = decimal::from_string("0.001");
  cr_assert_str_eq(d.to_string().c_str(), "0.001");
}

Test(from_string, scientific_positive_exp) {
  decimal d = decimal::from_string("2.34e56");
  cr_assert_eq(d.mantissa(), 2340000000000000000ULL);
  cr_assert_eq(d.exponent(), 38);
}

Test(from_string, scientific_negative_exp) {
  decimal d = decimal::from_string("5.6e-3");
  cr_assert_str_eq(d.to_string().c_str(), "0.0056");
}

Test(from_string, scientific_uppercase) {
  decimal d = decimal::from_string("1E10");
  cr_assert_eq(d.mantissa(), 1000000000000000000ULL);
  cr_assert_eq(d.exponent(), -8);
}

Test(from_string, scientific_positive_sign) {
  decimal d = decimal::from_string("3.14e+2");
  cr_assert_str_eq(d.to_string().c_str(), "314");
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
  cr_assert_str_eq(d.to_string().c_str(), "42");
}

Test(from_string, positive_sign) {
  decimal d = decimal::from_string("+99");
  cr_assert_str_eq(d.to_string().c_str(), "99");
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
  cr_assert_str_eq(d.to_string().c_str(), "123");
  cr_assert_str_eq(end, "abc");
}

Test(from_string, e_no_digits_after) {
  const char* end = nullptr;
  decimal d = decimal::from_string("5e", &end);
  cr_assert_str_eq(d.to_string().c_str(), "5");
  cr_assert_str_eq(end, "e");
}

Test(from_string, e_no_digits_after_sign) {
  const char* end = nullptr;
  decimal d = decimal::from_string("5e+", &end);
  cr_assert_str_eq(d.to_string().c_str(), "5");
  cr_assert_str_eq(end, "e+");
}

Test(from_string, just_zero) {
  decimal d = decimal::from_string("0");
  cr_assert(d.is_zero());
  cr_assert_str_eq(d.to_string().c_str(), "0");
}

Test(from_string, negative_zero) {
  decimal d = decimal::from_string("-0");
  cr_assert(d.is_zero());
  cr_assert(d.is_negative());
}

Test(from_string, multiple_leading_zeros) {
  decimal d = decimal::from_string("0000.0001");
  cr_assert_str_eq(d.to_string().c_str(), "0.0001");
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

Test(to_string, zero) {
  cr_assert_str_eq(decimal().to_string().c_str(), "0");
}

Test(to_string, negative_zero) {
  cr_assert_str_eq(decimal::zero(NEGATIVE).to_string().c_str(), "-0");
}

Test(to_string, nan) {
  cr_assert_str_eq(decimal::nan().to_string().c_str(), "nan");
}

Test(to_string, pos_inf) {
  cr_assert_str_eq(decimal::inf().to_string().c_str(), "inf");
}

Test(to_string, neg_inf) {
  cr_assert_str_eq(decimal::inf(NEGATIVE).to_string().c_str(), "-inf");
}

Test(to_string, integer) {
  decimal d = decimal::from_string("42");
  cr_assert_str_eq(d.to_string().c_str(), "42");
}

Test(to_string, no_trailing_zeros) {
  decimal d = decimal::from_string("1.50");
  cr_assert_str_eq(d.to_string().c_str(), "1.5");
}

Test(to_string, no_trailing_dot) {
  decimal d = decimal::from_string("100");
  cr_assert_str_eq(d.to_string().c_str(), "100");
}

Test(to_string, no_leading_zeros_integer) {
  decimal d = decimal::from_string("007");
  cr_assert_str_eq(d.to_string().c_str(), "7");
}

Test(to_string, fraction_less_than_one) {
  decimal d = decimal::from_string("0.5");
  cr_assert_str_eq(d.to_string().c_str(), "0.5");
}

Test(to_string, small_fraction) {
  decimal d = decimal::from_string("0.000123");
  cr_assert_str_eq(d.to_string().c_str(), "0.000123");
}

Test(to_string, large_integer) {
  decimal d = decimal::from_string("1e10");
  cr_assert_str_eq(d.to_string().c_str(), "10000000000");
}

Test(to_string, roundtrip_string) {
  const char* cases[] = {"1", "0.1", "0.001", "123.456", "1000000",
                         "99", "0.99", "12345678901234567"};
  for (auto s : cases) {
    decimal d = decimal::from_string(s);
    std::string result = d.to_string();
    cr_assert_str_eq(result.c_str(), s,
                     "roundtrip failed for \"%s\": got \"%s\"", s,
                     result.c_str());
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
  cr_assert_str_eq(c.to_string().c_str(), "4");

  cr_assert_eq(1.5 + 2.5, 4.0);
}

Test(addition, different_exponents) {
  decimal a = decimal::from_string("1000");
  decimal b = decimal::from_string("0.001");
  decimal c = a + b;
  cr_assert_str_eq(c.to_string().c_str(), "1000.001");

  cr_assert_eq(1000.0 + 0.001, 1000.001);
}

Test(addition, negative_result) {
  decimal a = decimal::from_string("3");
  decimal b = decimal::from_string("-5");
  decimal c = a + b;
  cr_assert_str_eq(c.to_string().c_str(), "-2");

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
  cr_assert_str_eq((a - b).to_string().c_str(), "7");

  cr_assert_eq(10.0 - 3.0, 7.0);
}

Test(subtraction, result_negative) {
  decimal a = decimal::from_string("3");
  decimal b = decimal::from_string("10");
  cr_assert_str_eq((a - b).to_string().c_str(), "-7");

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
  cr_assert_str_eq((a * b).to_string().c_str(), "42");

  cr_assert_eq(6.0 * 7.0, 42.0);
}

Test(multiplication, decimal) {
  decimal a = decimal::from_string("1.5");
  decimal b = decimal::from_string("4");
  cr_assert_str_eq((a * b).to_string().c_str(), "6");

  cr_assert_eq(1.5 * 4.0, 6.0);
}

Test(multiplication, negative) {
  decimal a = decimal::from_string("-3");
  decimal b = decimal::from_string("5");
  cr_assert_str_eq((a * b).to_string().c_str(), "-15");

  cr_assert_eq(-3.0 * 5.0, -15.0);
}

Test(multiplication, neg_times_neg) {
  decimal a = decimal::from_string("-3");
  decimal b = decimal::from_string("-5");
  cr_assert_str_eq((a * b).to_string().c_str(), "15");

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
  cr_assert_str_eq((a / b).to_string().c_str(), "5");

  cr_assert_eq(10.0 / 2.0, 5.0);
}

Test(division, decimal_result) {
  decimal a = decimal::from_string("1");
  decimal b = decimal::from_string("4");
  cr_assert_str_eq((a / b).to_string().c_str(), "0.25");

  cr_assert_eq(1.0 / 4.0, 0.25);
}

Test(division, negative) {
  decimal a = decimal::from_string("10");
  decimal b = decimal::from_string("-2");
  cr_assert_str_eq((a / b).to_string().c_str(), "-5");

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
  cr_assert_str_eq(b.to_string().c_str(), "-5");

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
  cr_assert_str_eq(a.abs().to_string().c_str(), "5");

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
  cr_assert_str_eq(a.to_string().c_str(), "15");
}

Test(compound, minus_eq) {
  decimal a = decimal::from_string("10");
  a -= decimal::from_string("3");
  cr_assert_str_eq(a.to_string().c_str(), "7");
}

Test(compound, times_eq) {
  decimal a = decimal::from_string("6");
  a *= decimal::from_string("7");
  cr_assert_str_eq(a.to_string().c_str(), "42");
}

Test(compound, div_eq) {
  decimal a = decimal::from_string("10");
  a /= decimal::from_string("4");
  cr_assert_str_eq(a.to_string().c_str(), "2.5");
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
  decimal result;

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
  cr_assert_str_eq(c.to_string().c_str(), "8e3000");
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
  cr_assert_str_eq(c.to_string().c_str(), "1");

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
  cr_assert_str_eq(d.to_string().c_str(), "42.5");
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
// Sign enum
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
  decimal d = decimal::from_string("1.23e100");
  cr_assert_str_eq(d.to_string().c_str(), "1.23e100");
}

Test(to_string, scientific_large_negative_exp) {
  decimal d = decimal::from_string("4.56e-100");
  cr_assert_str_eq(d.to_string().c_str(), "4.56e-100");
}

Test(to_string, scientific_single_digit) {
  decimal d = decimal::from_string("7e3000");
  cr_assert_str_eq(d.to_string().c_str(), "7e3000");
}

Test(to_string, scientific_many_digits) {
  decimal d = decimal::from_string("1.234567890123456789e1000");
  std::string s = d.to_string();
  cr_assert_str_eq(s.c_str(), "1.234567890123456789e1000");
}

Test(to_string, plain_within_threshold) {
  decimal d = decimal::from_string("1e19");
  std::string s = d.to_string();
  cr_assert_str_eq(s.c_str(), "10000000000000000000");
}

Test(to_string, scientific_just_beyond_threshold) {
  decimal d = decimal::from_string("1e20");
  cr_assert_str_eq(d.to_string().c_str(), "1e20");
}

Test(to_string, plain_small_negative_dp) {
  decimal d = decimal::from_string("1e-21");
  std::string s = d.to_string();
  std::string expected = "0." + std::string(20, '0') + "1";
  cr_assert_str_eq(s.c_str(), expected.c_str());
}

Test(to_string, scientific_small_beyond_threshold) {
  decimal d = decimal::from_string("1e-22");
  std::string s = d.to_string();
  cr_assert_str_eq(s.c_str(), "1e-22");
}

Test(to_string, scientific_roundtrip) {
  const char* cases[] = {"1.23e100", "4.56e-100", "1e3000", "9.99e-3000",
                         "1.234567890123456789e1000"};
  for (auto s : cases) {
    decimal d = decimal::from_string(s);
    std::string s1 = d.to_string();
    decimal d2 = decimal::from_string(s1);
    cr_assert(d == d2, "roundtrip failed for \"%s\" → \"%s\"", s, s1.c_str());
  }
}

// ---------------------------------------------------------------------------
// operator bool
// ---------------------------------------------------------------------------

Test(operator_bool, zero_is_false) {
  decimal d;
  cr_assert(!static_cast<bool>(d));
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
  decimal zero;
  if (nonzero) {} else { cr_assert_fail("nonzero should be truthy"); }
  if (!zero) {} else { cr_assert_fail("zero should be falsy"); }
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
  cr_assert_str_eq(d.mul_pow10(0).to_string().c_str(), "1.5");
  cr_assert_str_eq(d.mul_pow10(1).to_string().c_str(), "15");
  cr_assert_str_eq(d.mul_pow10(2).to_string().c_str(), "150");
  cr_assert_str_eq(d.mul_pow10(-1).to_string().c_str(), "0.15");
  cr_assert_str_eq(d.mul_pow10(-3).to_string().c_str(), "0.0015");
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
  cr_assert_str_eq(decimal::pow10(0).to_string().c_str(), "1");
  cr_assert_str_eq(decimal::pow10(1).to_string().c_str(), "10");
  cr_assert_str_eq(decimal::pow10(2).to_string().c_str(), "100");
  cr_assert_str_eq(decimal::pow10(-1).to_string().c_str(), "0.1");
  cr_assert_str_eq(decimal::pow10(-3).to_string().c_str(), "0.001");
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
              "\"%s\" → \"%s\" (%zu chars, max %d)", s, result.c_str(),
              result.size(), decimal::max_string_length);
  }
}
