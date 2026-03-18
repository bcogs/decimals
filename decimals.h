#ifndef DECIMALS_H__
#define DECIMALS_H__

#include <cstdint>
#include <limits>
#include <string>
#include <iosfwd>

namespace decimals {

enum sign { POSITIVE, NEGATIVE };

// decimal is a floating-point number using powers of 10 instead of powers of 2.
// Behaves like a double: + -= < etc are provided, and nans/inf too.
//
// A round-trip conversion from decimal to string and back yields the same value
// (modulo leading 0s, and trailing zeros or dot).
//
// Conversion from double and int64 to decimal isn't lossy (all values fit
// in 19 significant digits), but conversion from decimal to double and int64
// is.  Conversion from uint64 to decimal is exact for values in
// [0, max_exact_uint64] (10^19 - 1).
//
// All comparison and arithmetic operations follow IEEE 754 semantics for NaN
// and infinity (e.g. NaN != NaN, NaN unordered with everything,
// inf - inf = NaN, 0/0 = NaN, nonzero/0 = +/-inf, etc.).
class decimal {
 public:
  // Default construction leaves the value uninitialized, like double.
  decimal() = default;

  // Constructs a decimal from a double, preserving the exact decimal
  // representation that printf("%.17g") would produce.
  explicit decimal(double d);

  // Constructs a decimal from an integer.  The conversion is exact for all
  // int64 values and for uint64 values up to max_exact_uint64.
  explicit decimal(int64_t v);
  explicit decimal(uint64_t v);

  // --- Named constructors ---------------------------------------------------

  static decimal nan();  // quiet NaN

  static decimal inf(sign s = POSITIVE);  // infinity

  static decimal zero(sign s = POSITIVE);  // zero

  static decimal from_double(double d);  // same as decimal(double d)

  // Parses a decimal string, similar to strtod.
  //
  // Accepted formats:
  //   [whitespace] [+|-] digits[.digits] [e|E [+|-] digits]
  //   [whitespace] [+|-] 0x hexdigits[.hexdigits] p|P [+|-] digits
  //   [whitespace] [+|-] nan
  //   [whitespace] [+|-] inf[inity]
  //
  // If endptr is not null, *endptr is set to point past the last consumed
  // character. If no valid conversion could be performed, returns zero and
  // *endptr is set to str.
  static decimal from_string(const char* str, const char** endptr = nullptr);
  static decimal from_string(const std::string& str);

  // --- Accessors -------------------------------------------------------------

  // Returns the power-of-10 exponent.
  //
  // The value of a finite decimal is mantissa() * 10^exponent().  The mantissa
  // is always normalized to [10^18, 10^19) — exactly 19 digits, like a
  // normalized IEEE 754 significand — so the exponent absorbs the scaling:
  //
  //   "123"       →  mantissa = 1230000000000000000,  exponent = -16
  //   "1.23"      →  mantissa = 1230000000000000000,  exponent = -18
  //   "12300"     →  mantissa = 1230000000000000000,  exponent = -14
  //   "0.00456"   →  mantissa = 4560000000000000000,  exponent = -21
  //   "7e10"      →  mantissa = 7000000000000000000,  exponent =  -8
  //   "1e20"      →  mantissa = 1000000000000000000,  exponent =   2
  int64_t exponent() const;

  // Returns the mantissa (significand).  For non-zero finite values this is
  // always in [10^18, 10^19) — exactly 19 decimal digits.  For zero, inf, and
  // NaN, see is_zero()/is_inf()/is_nan().
  uint64_t mantissa() const;

  // --- Queries ---------------------------------------------------------------

  bool is_nan() const;
  bool is_inf() const;
  bool is_zero() const;
  bool is_finite() const;
  bool is_negative() const;

  // --- Conversions -----------------------------------------------------------

  // Converts to the double nearest to this decimal's exact value.
  // Most decimals are not exactly representable in binary floating point
  // (e.g. decimal("0.1")), so the result is rounded to the closest double.
  double to_double() const;

  // Cast operators — behave like the corresponding double conversions.
  explicit operator bool() const;    // false iff zero; NaN is true (like double)
  explicit operator double() const;  // same as to_double()
  explicit operator int64_t() const;   // truncates toward zero (like double→int)
  explicit operator uint64_t() const;  // truncates toward zero (like double→unsigned)

  // Converts to a decimal string with exact representation.
  // No leading zeros (except the single "0" before a decimal point in numbers
  // like "0.5"), no trailing zeros, no trailing decimal point.
  // Uses plain decimal when compact (e.g. "123.456", "0.001"), scientific
  // notation otherwise (e.g. "1.23e100").
  // NaN produces "nan", infinities produce "inf" or "-inf".
  std::string to_string() const; // XXX should have a char* version

  // --- Arithmetic (NaN/inf follow IEEE 754 semantics) ------------------------

  decimal operator-() const;
  decimal operator+(const decimal& rhs) const;
  decimal operator-(const decimal& rhs) const;
  decimal operator*(const decimal& rhs) const;
  decimal operator/(const decimal& rhs) const;

  decimal& operator+=(const decimal& rhs);
  decimal& operator-=(const decimal& rhs);
  decimal& operator*=(const decimal& rhs);
  decimal& operator/=(const decimal& rhs);

  decimal abs() const;  // absolute value

  // Multiplies by 10^p.  This just adjusts the exponent — O(1), no rounding.
  // For NaN/inf the exponent is ignored, so the value is returned unchanged.
  decimal mul_pow10(int64_t p) const;

  // Returns 10^p.  O(1) — mantissa is 1, exponent is p.
  static decimal pow10(int64_t p);

  // Returns floor(log10(abs(x))) for finite non-zero values.
  // This is the number of digits minus one, i.e. ilog10(999) = 2, ilog10(1000) = 3.
  // O(1) — derived directly from the exponent and leading digit of the mantissa.
  // Returns 0 for zero, NaN, and infinity (no error signaling).
  int64_t ilog10() const;

  // --- Comparison (NaN-aware, IEEE 754 semantics) ----------------------------

  bool operator==(const decimal& rhs) const;
  bool operator!=(const decimal& rhs) const;
  bool operator<(const decimal& rhs) const;
  bool operator<=(const decimal& rhs) const;
  bool operator>(const decimal& rhs) const;
  bool operator>=(const decimal& rhs) const;

  // --- Stream output ---------------------------------------------------------

  // Writes the same string as to_string() to the stream.
  friend std::ostream& operator<<(std::ostream& os, const decimal& d);

  // Maximum number of characters to_string() can produce, not counting '\0'.
  // Can be used to size a buffer: char buf[decimal::max_string_length + 1].
  static constexpr int max_string_length = 42;

  // Largest uint64_t that converts to decimal without rounding (10^19 - 1).
  // All int64_t values (up to 19 digits) are always exact.
  static constexpr uint64_t max_exact_uint64 = 9999999999999999999ULL;

 protected:
  uint64_t x[2];
};

}  // namespace decimals

// --- std::numeric_limits specialization --------------------------------------

template <>
class std::numeric_limits<decimals::decimal> {
 public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = false;
  static constexpr bool is_exact = false;
  static constexpr bool has_infinity = true;
  static constexpr bool has_quiet_NaN = true;
  static constexpr bool has_signaling_NaN = false;
  static constexpr std::float_denorm_style has_denorm = std::denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr std::float_round_style round_style = std::round_to_nearest;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 19;
  static constexpr int digits10 = 19;
  static constexpr int max_digits10 = 19;
  static constexpr int radix = 10;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static decimals::decimal min() noexcept;        // smallest positive
  static decimals::decimal max() noexcept;        // largest positive
  static decimals::decimal lowest() noexcept;     // most negative
  static decimals::decimal epsilon() noexcept;    // gap at 1.0 (= 10^-18)
  static decimals::decimal round_error() noexcept;
  static decimals::decimal infinity() noexcept;
  static decimals::decimal quiet_NaN() noexcept;
  static decimals::decimal signaling_NaN() noexcept;
  static decimals::decimal denorm_min() noexcept;
};

#endif
