#include "decimals.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <ostream>

namespace decimals {

// ---------------------------------------------------------------------------
// Fast-path optimization strategy
//
// All operators check whether both operands are non-special (i.e. neither
// inf nor NaN) before doing anything else.  This single test — a comparison
// of the raw exponent field against kSpecialExp — covers the common case
// and avoids cascading is_nan/is_inf/is_zero checks on the hot path.
//
// Within the fast path, zero (mantissa == 0) is handled as a finite value
// because it is far more common than inf or NaN, especially in comparisons.
// Only when at least one operand IS special do we fall through to the slow
// path, which tests for NaN and infinity individually.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// The internal representation uses 128 bits:
//
//   x[0] bit layout:
//     Bit 63:    sign (0 = positive, 1 = negative)
//     Bits 0-62: biased exponent (63 bits, bias = 4611686018427387903)
//
//   x[1]: mantissa (decimal significand, always normalized to
//          [10^18, 10^19) for non-zero finite values — exactly 19
//          significant digits, analogous to an IEEE 754 normalized
//          significand)
//
// The represented value is:
//   (-1)^sign * mantissa * 10^(stored_exponent - bias)
//
// Special encodings (stored exponent = all 63 bits set):
//   mantissa == 0  =>  +/-infinity
//   mantissa != 0  =>  NaN
//
// Zero is represented as mantissa == 0 with any non-special exponent
// (canonically stored_exponent = 0). Negative zero is supported.
//
// Exponent range: roughly +/-4.6e18.
//
// Mantissa precision: exactly 19 significant decimal digits.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Constants (internal to this TU, visible throughout namespace decimals)
// ---------------------------------------------------------------------------

static constexpr int kExponentBits = 63;
static constexpr uint64_t kExponentMask = (1ULL << 63) - 1;
static constexpr int64_t kExponentBias =
    static_cast<int64_t>((kExponentMask - 1) / 2);  // 4611686018427387903
static constexpr uint64_t kSignBit = 1ULL << 63;
static constexpr uint64_t kSpecialExp = kExponentMask;
static constexpr int64_t kMaxExponent = kExponentBias;
static constexpr int64_t kMinExponent = -kExponentBias;

// Mantissa normalization range: [kNormMin, kNormMax) = [10^18, 10^19).
static constexpr uint64_t kNormMin = 1000000000000000000ULL;   // 10^18
static constexpr uint64_t kNormMax = 10000000000000000000ULL;  // 10^19

// Power-of-10 lookup table: kPow10[i] = 10^i for i in [0, 19].
static constexpr uint64_t kPow10[20] = {
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

// Returns the number of decimal digits in v (1 for v=0).
static int count_digits_u64(uint64_t v) {
  if (v < 10000000000ULL) {
    if (v < 100000ULL) {
      if (v < 100ULL) return v < 10ULL ? 1 : 2;
      return v < 1000ULL ? 3 : (v < 10000ULL ? 4 : 5);
    }
    if (v < 10000000ULL) return v < 1000000ULL ? 6 : 7;
    return v < 100000000ULL ? 8 : (v < 1000000000ULL ? 9 : 10);
  }
  if (v < 1000000000000000ULL) {
    if (v < 1000000000000ULL) return v < 100000000000ULL ? 11 : 12;
    return v < 10000000000000ULL ? 13 : (v < 100000000000000ULL ? 14 : 15);
  }
  if (v < 100000000000000000ULL) return v < 10000000000000000ULL ? 16 : 17;
  return v < 1000000000000000000ULL ? 18 : (v < 10000000000000000000ULL ? 19 : 20);
}

// to_string() uses plain decimal when the number of digits before the
// decimal point (dp) is in [-kPlainDpLimit, kPlainDpLimit].  Beyond that,
// scientific notation keeps the output within decimal::max_string_length.
static constexpr int64_t kPlainDpLimit = 20;

namespace {

// ---------------------------------------------------------------------------
// decimalImpl — inherits from decimal to access the protected x[2] storage
// and adds private helpers used by the implementation.
// ---------------------------------------------------------------------------

class decimalImpl : public decimal {
 public:
  static decimalImpl& of(decimal& d) {
    return static_cast<decimalImpl&>(d);
  }
  static const decimalImpl& of(const decimal& d) {
    return static_cast<const decimalImpl&>(d);
  }

  uint64_t raw_exponent() const { return x[0] & kExponentMask; }
  void set_raw_exponent(uint64_t e) {
    x[0] = (x[0] & ~kExponentMask) | (e & kExponentMask);
  }
  void set_sign(Sign s) {
    if (s == NEGATIVE)
      x[0] |= kSignBit;
    else
      x[0] &= ~kSignBit;
  }
  void flip_sign() { x[0] ^= kSignBit; }

  Sign sign() const { return (x[0] & kSignBit) ? NEGATIVE : POSITIVE; }
  int64_t biased_exponent() const {
    return static_cast<int64_t>(raw_exponent()) - kExponentBias;
  }
  uint64_t mant() const { return x[1]; }

  bool is_special() const { return raw_exponent() == kSpecialExp; }
  bool is_nan() const { return is_special() && x[1] != 0; }
  bool is_inf() const { return is_special() && x[1] == 0; }
  bool is_zero() const { return !is_special() && x[1] == 0; }

  static uint64_t fit_to_uint64(__uint128_t value, int64_t& exponent);

  static decimal make(Sign s, int64_t exponent, uint64_t mantissa);
  static decimal make_special(Sign s, uint64_t mantissa);

  static int compare_abs(const decimal& a, const decimal& b);

  static decimal add_abs(const decimal& a, const decimal& b, Sign rs);
  static decimal sub_abs(const decimal& larger, const decimal& smaller,
                                Sign rs);
};

static_assert(sizeof(decimalImpl) == sizeof(decimal), "decimalImpl and decimal must have the exact same members");

// Result sign for multiplication/division: same signs → POSITIVE, different → NEGATIVE.
Sign mul_sign(Sign a, Sign b) {
  static_assert(POSITIVE == 0 && NEGATIVE == 1, "the expression in the return below won't work");
  return (Sign) (a ^ b);
}

uint64_t decimalImpl::fit_to_uint64(__uint128_t value, int64_t& exponent) {
  while (value > static_cast<__uint128_t>(UINT64_MAX)) {
    __uint128_t next = value / 10;
    int rem = static_cast<int>(value - next * 10);
    value = next;
    exponent++;
    if (rem >= 5) value++;
  }
  return static_cast<uint64_t>(value);
}

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------

decimal decimalImpl::make_special(Sign s, uint64_t mantissa) {
  decimal d;
  auto& i = of(d);
  i.x[0] = 0;
  i.x[1] = mantissa;
  i.set_raw_exponent(kSpecialExp);
  i.set_sign(s);
  return d;
}

decimal decimalImpl::make(Sign s, int64_t exponent, uint64_t mantissa) {
  if (mantissa == 0) return decimal::zero(s);

  int nd = count_digits_u64(mantissa);

  if (nd > 19) {
    // nd == 20 (max for uint64_t).  Divide by 10 once and round.
    uint64_t next = mantissa / 10;
    int rem = static_cast<int>(mantissa - next * 10);
    mantissa = next;
    exponent++;
    if (rem >= 5) {
      mantissa++;
      // Rounding carry: e.g. 9999999999999999999 + 1 = kNormMax.
      if (mantissa >= kNormMax) {
        mantissa /= 10;
        exponent++;
      }
    }
  } else if (nd < 19) {
    // Pad mantissa up to 19 digits in one multiply.
    int deficit = 19 - nd;
    mantissa *= kPow10[deficit];
    // Check underflow before subtracting to avoid int64_t wrap.
    if (exponent < kMinExponent + deficit) return decimal::zero(s);
    exponent -= deficit;
  }

  if (exponent > kMaxExponent) return decimal::inf(s);
  if (exponent < kMinExponent) return decimal::zero(s);

  decimal d;
  auto& i = of(d);
  i.x[0] = 0;
  i.x[1] = mantissa;
  i.set_raw_exponent(static_cast<uint64_t>(exponent + kExponentBias));
  i.set_sign(s);
  return d;
}

// ---------------------------------------------------------------------------
// Magnitude comparison — with fixed-range mantissas this is trivial:
// compare exponents, then mantissas as plain integers.
// ---------------------------------------------------------------------------

int decimalImpl::compare_abs(const decimal& a, const decimal& b) {
  auto& ai = of(a);
  auto& bi = of(b);
  uint64_t ma = ai.mant(), mb = bi.mant();
  if (ma == 0 && mb == 0) return 0;
  if (ma == 0) return -1;
  if (mb == 0) return 1;

  int64_t ea = ai.biased_exponent(), eb = bi.biased_exponent();
  if (ea != eb) return ea < eb ? -1 : 1;
  if (ma != mb) return ma < mb ? -1 : 1;
  return 0;
}

// ---------------------------------------------------------------------------
// Arithmetic helpers
// ---------------------------------------------------------------------------

decimal decimalImpl::add_abs(const decimal& a, const decimal& b,
                                    Sign rs) {
  auto& ai = of(a);
  auto& bi = of(b);
  uint64_t ma = ai.mant(), mb = bi.mant();
  int64_t ea = ai.biased_exponent(), eb = bi.biased_exponent();

  if (ma == 0)
    return mb == 0 ? decimal::zero(rs) : make(rs, eb, mb);
  if (mb == 0) return make(rs, ea, ma);

  if (ea < eb) {
    std::swap(ea, eb);
    std::swap(ma, mb);
  }

  int64_t diff = ea - eb;
  if (diff >= 20) return make(rs, ea, ma);

  __uint128_t scaled = static_cast<__uint128_t>(ma) * kPow10[diff] + mb;

  int64_t rexp = eb;
  uint64_t rm = fit_to_uint64(scaled, rexp);
  return make(rs, rexp, rm);
}

decimal decimalImpl::sub_abs(const decimal& larger,
                                    const decimal& smaller, Sign rs) {
  auto& li = of(larger);
  auto& si = of(smaller);
  uint64_t ma = li.mant(), mb = si.mant();
  int64_t ea = li.biased_exponent(), eb = si.biased_exponent();

  if (mb == 0) return make(rs, ea, ma);

  __uint128_t result;
  int64_t rexp;

  if (ea >= eb) {
    int64_t diff = ea - eb;
    if (diff >= 20) return make(rs, ea, ma);
    result = static_cast<__uint128_t>(ma) * kPow10[diff]
             - static_cast<__uint128_t>(mb);
    rexp = eb;
  } else {
    int64_t diff = eb - ea;
    if (diff >= 20) return make(rs, ea, ma);
    result = static_cast<__uint128_t>(ma)
             - static_cast<__uint128_t>(mb) * kPow10[diff];
    rexp = ea;
  }

  uint64_t rm = fit_to_uint64(result, rexp);
  return make(rs, rexp, rm);
}

// ---------------------------------------------------------------------------
// Scientific-notation formatter: [-]d[.ddd...]e[-]ddd...
// ---------------------------------------------------------------------------

static std::string format_scientific(Sign s, const std::string& digits,
                                     int64_t sci_exp) {
  std::string result;
  if (s == NEGATIVE) result += '-';
  result += digits[0];
  if (digits.size() > 1) {
    result += '.';
    result.append(digits, 1, std::string::npos);
  }
  result += 'e';
  result += std::to_string(sci_exp);
  return result;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Helper for std::numeric_limits (not in header, accessible in this TU only)
// ---------------------------------------------------------------------------

static decimal make_with_fields(Sign s, int64_t exponent, uint64_t mantissa) {
  return decimalImpl::make(s, exponent, mantissa);
}

// ---------------------------------------------------------------------------
// decimal public methods
// ---------------------------------------------------------------------------

decimal::decimal() {
  x[0] = 0;
  x[1] = 0;
}

decimal::decimal(double d) { *this = from_double(d); }

decimal::decimal(int64_t v) {
  if (v == 0) {
    x[0] = 0;
    x[1] = 0;
    return;
  }
  Sign s = POSITIVE;
  uint64_t u;
  if (v < 0) {
    s = NEGATIVE;
    // Safe even for INT64_MIN: -(int64_t)INT64_MIN is UB, but the
    // unsigned cast is well-defined and produces the right magnitude.
    u = static_cast<uint64_t>(-static_cast<__int128>(v));
  } else {
    u = static_cast<uint64_t>(v);
  }
  *this = decimalImpl::make(s, 0, u);
}

decimal::decimal(uint64_t v) {
  if (v == 0) {
    x[0] = 0;
    x[1] = 0;
    return;
  }
  *this = decimalImpl::make(POSITIVE, 0, v);
}

decimal decimal::nan() { return decimalImpl::make_special(POSITIVE, 1); }

decimal decimal::inf(Sign s) { return decimalImpl::make_special(s, 0); }

decimal decimal::zero(Sign s) {
  decimal d;
  auto& i = decimalImpl::of(d);
  i.x[0] = 0;
  i.x[1] = 0;
  i.set_sign(s);
  return d;
}

decimal decimal::from_double(double d) {
  if (__builtin_expect(!std::isfinite(d), 0)) {
    if (std::isnan(d)) return nan();
    return inf(d < 0 ? NEGATIVE : POSITIVE);
  }
  if (d == 0.0) return zero(std::signbit(d) ? NEGATIVE : POSITIVE);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.17g", d);
  return from_string(buf);
}

// ---------------------------------------------------------------------------
// Accessors / queries
// ---------------------------------------------------------------------------

bool decimal::is_negative() const {
  return decimalImpl::of(*this).sign() == NEGATIVE;
}

int64_t decimal::exponent() const {
  return decimalImpl::of(*this).biased_exponent();
}

uint64_t decimal::mantissa() const { return decimalImpl::of(*this).mant(); }

bool decimal::is_nan() const { return decimalImpl::of(*this).is_nan(); }
bool decimal::is_inf() const { return decimalImpl::of(*this).is_inf(); }
bool decimal::is_zero() const { return decimalImpl::of(*this).is_zero(); }

bool decimal::is_finite() const {
  return !decimalImpl::of(*this).is_special();
}

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

double decimal::to_double() const {
  auto& impl = decimalImpl::of(*this);
  if (__builtin_expect(!impl.is_special(), 1)) {
    if (impl.mant() == 0)
      return impl.sign() == NEGATIVE ? -0.0 : 0.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%llue%lld",
                  impl.sign() == NEGATIVE ? "-" : "",
                  static_cast<unsigned long long>(impl.mant()),
                  static_cast<long long>(impl.biased_exponent()));
    return std::strtod(buf, nullptr);
  }
  if (impl.mant() != 0) return std::numeric_limits<double>::quiet_NaN();
  return impl.sign() == NEGATIVE ? -std::numeric_limits<double>::infinity()
                                 : std::numeric_limits<double>::infinity();
}

decimal::operator bool() const {
  return !is_zero();
}

decimal::operator double() const {
  return to_double();
}

decimal::operator int64_t() const {
  auto& impl = decimalImpl::of(*this);
  if (impl.is_special()) {
    if (impl.is_nan()) return 0;
    return impl.sign() == NEGATIVE ? INT64_MIN : INT64_MAX;
  }
  if (impl.mant() == 0) return 0;

  int64_t exp = impl.biased_exponent();
  uint64_t m = impl.mant();

  if (exp < -18) return 0;  // |value| < 1
  if (exp > 0) {
    // m >= 10^18, m * 10 >= 10^19 > INT64_MAX
    return impl.sign() == NEGATIVE ? INT64_MIN : INT64_MAX;
  }

  // exp in [-18, 0]: value = m / 10^(-exp)
  uint64_t abs_val = (exp == 0) ? m : m / kPow10[static_cast<int>(-exp)];

  if (impl.sign() == NEGATIVE) {
    if (abs_val >= 9223372036854775808ULL) return INT64_MIN;
    return -static_cast<int64_t>(abs_val);
  }
  if (abs_val > 9223372036854775807ULL) return INT64_MAX;
  return static_cast<int64_t>(abs_val);
}

decimal::operator uint64_t() const {
  auto& impl = decimalImpl::of(*this);
  if (impl.is_special()) {
    if (impl.is_nan()) return 0;
    return impl.sign() == NEGATIVE ? 0ULL : UINT64_MAX;
  }
  if (impl.mant() == 0) return 0;
  if (impl.sign() == NEGATIVE) return 0;

  int64_t exp = impl.biased_exponent();
  uint64_t m = impl.mant();

  if (exp < -18) return 0;
  if (exp >= 2) return UINT64_MAX;
  if (exp == 1) {
    // m * 10, check overflow
    if (m > UINT64_MAX / 10) return UINT64_MAX;
    return m * 10;
  }
  if (exp == 0) return m;

  // exp in [-18, -1]
  return m / kPow10[static_cast<int>(-exp)];
}

decimal decimal::mul_pow10(int64_t p) const {
  auto& impl = decimalImpl::of(*this);
  if (impl.is_special() || impl.mant() == 0) return *this;
  __int128 new_exp = static_cast<__int128>(impl.biased_exponent()) + p;
  if (new_exp > kMaxExponent) return decimal::inf(impl.sign());
  if (new_exp < kMinExponent) return decimal::zero(impl.sign());
  decimal d = *this;
  decimalImpl::of(d).set_raw_exponent(
      static_cast<uint64_t>(static_cast<int64_t>(new_exp) + kExponentBias));
  return d;
}

decimal decimal::pow10(int64_t p) {
  return decimalImpl::make(POSITIVE, p, 1);
}

int64_t decimal::ilog10() const {
  auto& impl = decimalImpl::of(*this);
  if (impl.is_special() || impl.mant() == 0) return 0;
  // mantissa is in [10^18, 10^19), so floor(log10(mantissa)) = 18 always.
  return impl.biased_exponent() + 18;
}

std::string decimal::to_string() const {
  auto& impl = decimalImpl::of(*this);

  // Fast path: finite non-zero.
  if (__builtin_expect(!impl.is_special() && impl.mant() != 0, 1)) {
    // Strip trailing zeros from the mantissa before converting to a digit
    // string, so we never generate characters we would have to remove.
    uint64_t m = impl.mant();
    int64_t exp = impl.biased_exponent();
    while (m % 10 == 0) {
      m /= 10;
      exp++;
    }

    std::string digits = std::to_string(m);
    int n = static_cast<int>(digits.size());
    int64_t dp = n + exp;
    Sign s = impl.sign();

    // Use scientific notation when plain form would exceed max_string_length.
    if (dp > kPlainDpLimit || dp < -kPlainDpLimit) {
      return format_scientific(s, digits, dp - 1);
    }

    std::string prefix = s == NEGATIVE ? "-" : "";

    if (dp >= n) {
      return prefix + digits + std::string(static_cast<size_t>(dp - n), '0');
    }
    if (dp > 0) {
      return prefix + digits.substr(0, static_cast<size_t>(dp)) + "." +
             digits.substr(static_cast<size_t>(dp));
    }
    return prefix + "0." + std::string(static_cast<size_t>(-dp), '0') + digits;
  }

  // Slow path: special values and zero.
  if (impl.is_nan()) return "nan";
  if (impl.mant() == 0 && impl.is_special())
    return impl.sign() == NEGATIVE ? "-inf" : "inf";
  return impl.sign() == NEGATIVE ? "-0" : "0";
}

// ---------------------------------------------------------------------------
// from_string
// ---------------------------------------------------------------------------

decimal decimal::from_string(const std::string& str) {
  return from_string(str.c_str());
}

decimal decimal::from_string(const char* str, const char** endptr) {
  const char* orig = str;
  const char* p = str;

  while (std::isspace(static_cast<unsigned char>(*p))) p++;

  Sign s = POSITIVE;
  if (*p == '-') {
    s = NEGATIVE;
    p++;
  } else if (*p == '+') {
    p++;
  }

  // NaN (case-insensitive).
  if ((p[0] == 'n' || p[0] == 'N') && (p[1] == 'a' || p[1] == 'A') &&
      (p[2] == 'n' || p[2] == 'N')) {
    p += 3;
    if (endptr) *endptr = p;
    return nan();
  }

  // Infinity (case-insensitive).
  if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') &&
      (p[2] == 'f' || p[2] == 'F')) {
    p += 3;
    if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') &&
        (p[2] == 'i' || p[2] == 'I') && (p[3] == 't' || p[3] == 'T') &&
        (p[4] == 'y' || p[4] == 'Y')) {
      p += 5;
    }
    if (endptr) *endptr = p;
    return inf(s);
  }

  // Hexadecimal floating-point (C99): [+|-] 0x H[.H] p [+|-] D
  // Delegate to strtod, which handles hex floats natively.
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    char* strtod_end = nullptr;
    double val = std::strtod(p, &strtod_end);
    if (strtod_end == p) {
      if (endptr) *endptr = orig;
      return decimal();
    }
    if (endptr) *endptr = strtod_end;
    decimal result = from_double(val);
    if (s == NEGATIVE) result = -result;
    return result;
  }

  uint64_t mant = 0;
  int mant_digits = 0;
  int extra_digits = 0;
  bool seen_dot = false;
  bool seen_digit = false;
  bool in_leading_zeros = true;
  int digits_after_dot = 0;
  char first_dropped = '0';

  const char* before_e = p;

  while (*p) {
    if (*p == '.' && !seen_dot) {
      seen_dot = true;
      p++;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(*p))) break;

    seen_digit = true;
    if (seen_dot) digits_after_dot++;

    if (*p == '0' && in_leading_zeros) {
      p++;
      continue;
    }
    in_leading_zeros = false;

    if (mant_digits < 19) {
      mant = mant * 10 + static_cast<uint64_t>(*p - '0');
      mant_digits++;
    } else {
      if (extra_digits == 0) first_dropped = *p;
      extra_digits++;
    }
    p++;
  }

  if (!seen_digit) {
    if (endptr) *endptr = orig;
    return decimal();
  }

  before_e = p;

  if (first_dropped >= '5') mant++;

  int64_t e_exp = 0;
  if (*p == 'e' || *p == 'E') {
    p++;
    bool e_neg = false;
    if (*p == '-') {
      e_neg = true;
      p++;
    } else if (*p == '+') {
      p++;
    }
    if (!std::isdigit(static_cast<unsigned char>(*p))) {
      p = before_e;
    } else {
      while (std::isdigit(static_cast<unsigned char>(*p))) {
        // Saturate at 5e18 (> kMaxExponent) to avoid int64_t overflow.
        if (e_exp < 500000000000000000LL)
          e_exp = e_exp * 10 + (*p - '0');
        else
          e_exp = 5000000000000000000LL;
        p++;
      }
      if (e_neg) e_exp = -e_exp;
    }
  }

  if (endptr) *endptr = p;

  int64_t final_exp = e_exp - digits_after_dot + extra_digits;

  if (mant == 0) return zero(s);

  return decimalImpl::make(s, final_exp, mant);
}

// ---------------------------------------------------------------------------
// Arithmetic operators
// ---------------------------------------------------------------------------

decimal decimal::operator-() const {
  decimal d = *this;
  decimalImpl::of(d).flip_sign();
  return d;
}

decimal decimal::abs() const {
  decimal d = *this;
  decimalImpl::of(d).set_sign(POSITIVE);
  return d;
}

decimal decimal::operator+(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);

  // Fast path: both finite (including zero).
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    uint64_t ma = a.mant(), mb = b.mant();
    Sign sa = a.sign(), sb = b.sign();

    if (ma == 0) {
      if (mb == 0)
        return zero(sa == NEGATIVE && sb == NEGATIVE ? NEGATIVE : POSITIVE);
      return rhs;
    }
    if (mb == 0) return *this;

    if (sa == sb) return decimalImpl::add_abs(*this, rhs, sa);

    int cmp = decimalImpl::compare_abs(*this, rhs);
    if (cmp == 0) return zero();
    if (cmp > 0) return decimalImpl::sub_abs(*this, rhs, sa);
    return decimalImpl::sub_abs(rhs, *this, sb);
  }

  // Slow path: at least one inf or NaN.
  if (a.is_nan() || b.is_nan()) return nan();
  if (a.is_inf() && b.is_inf()) {
    if (a.sign() == b.sign()) return *this;
    return nan();
  }
  if (a.is_inf()) return *this;
  return rhs;
}

decimal decimal::operator-(const decimal& rhs) const { return *this + (-rhs); }

decimal decimal::operator*(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);

  // Fast path: both finite.
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    Sign rs = mul_sign(a.sign(), b.sign());
    uint64_t ma = a.mant(), mb = b.mant();
    if (ma == 0 || mb == 0) return zero(rs);

    __uint128_t prod = static_cast<__uint128_t>(ma) * mb;
    int64_t rexp = a.biased_exponent() + b.biased_exponent();
    uint64_t rm = decimalImpl::fit_to_uint64(prod, rexp);
    return decimalImpl::make(rs, rexp, rm);
  }

  // Slow path: at least one inf or NaN.
  if (a.is_nan() || b.is_nan()) return nan();
  Sign rs = mul_sign(a.sign(), b.sign());
  // inf * 0 = NaN; inf * anything_else = inf.
  if (a.is_zero() || b.is_zero()) return nan();
  return inf(rs);
}

decimal decimal::operator/(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);

  // Fast path: both finite (handles division by zero too, since 0 is finite).
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    Sign rs = mul_sign(a.sign(), b.sign());
    uint64_t ma = a.mant(), mb = b.mant();

    if (mb == 0) {
      if (ma == 0) return nan();  // 0/0
      return inf(rs);             // x/0
    }
    if (ma == 0) return zero(rs);  // 0/x

    __uint128_t scaled = static_cast<__uint128_t>(ma);
    int k = 0;
    while (k < 19) {
      __uint128_t next = scaled * 10;
      if (next / mb > static_cast<__uint128_t>(UINT64_MAX)) break;
      scaled = next;
      k++;
    }

    __uint128_t quotient = scaled / mb;
    __uint128_t remainder = scaled % mb;
    uint64_t rm = static_cast<uint64_t>(quotient);
    if (remainder * 2 >= mb) rm++;

    int64_t rexp = a.biased_exponent() - b.biased_exponent() - k;
    return decimalImpl::make(rs, rexp, rm);
  }

  // Slow path: at least one inf or NaN.
  if (a.is_nan() || b.is_nan()) return nan();
  Sign rs = mul_sign(a.sign(), b.sign());
  if (a.is_inf()) {
    if (b.is_inf()) return nan();  // inf/inf
    return inf(rs);                // inf/x
  }
  // a is finite, b is inf.
  return zero(rs);
}

decimal& decimal::operator+=(const decimal& rhs) {
  *this = *this + rhs;
  return *this;
}
decimal& decimal::operator-=(const decimal& rhs) {
  *this = *this - rhs;
  return *this;
}
decimal& decimal::operator*=(const decimal& rhs) {
  *this = *this * rhs;
  return *this;
}
decimal& decimal::operator/=(const decimal& rhs) {
  *this = *this / rhs;
  return *this;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool decimal::operator==(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);

  // Fast path: both finite.
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    uint64_t ma = a.mant(), mb = b.mant();
    // Zero equals zero regardless of sign.
    if (ma == 0 || mb == 0) return ma == mb;
    // Non-zero: equal iff same sign, exponent, and mantissa (= same bits).
    return a.x[0] == b.x[0] && ma == mb;
  }

  // Slow path.
  if (a.is_nan() || b.is_nan()) return false;
  if (a.is_inf() && b.is_inf()) return a.sign() == b.sign();
  return false;
}

bool decimal::operator!=(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);

  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    uint64_t ma = a.mant(), mb = b.mant();
    if (ma == 0 || mb == 0) return ma != mb;
    return a.x[0] != b.x[0] || ma != mb;
  }

  if (a.is_nan() || b.is_nan()) return true;
  if (a.is_inf() && b.is_inf()) return a.sign() != b.sign();
  return true;  // inf != finite
}

bool decimal::operator<(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);

  // Fast path: both finite.
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    uint64_t ma = a.mant(), mb = b.mant();
    Sign sa = a.sign(), sb = b.sign();

    if (ma == 0 && mb == 0) return false;
    if (sa != sb) return sa == NEGATIVE;  // negative < positive

    // Same sign.
    if (ma == 0) return sa == POSITIVE;  // 0 < positive; 0 is not < negative
    if (mb == 0) return sa == NEGATIVE;  // negative < 0; positive is not < 0

    // Both non-zero, same sign.  Compare magnitude inline.
    int64_t ea = a.biased_exponent(), eb = b.biased_exponent();
    int cmp;
    if (ea != eb)
      cmp = ea < eb ? -1 : 1;
    else if (ma != mb)
      cmp = ma < mb ? -1 : 1;
    else
      return false;  // equal
    return sa == NEGATIVE ? cmp > 0 : cmp < 0;
  }

  // Slow path: at least one inf or NaN.
  if (a.is_nan() || b.is_nan()) return false;
  Sign sa = a.sign(), sb = b.sign();
  if (a.is_inf() && b.is_inf()) return sa == NEGATIVE && sb == POSITIVE;
  if (a.is_inf()) return sa == NEGATIVE;
  return sb == POSITIVE;
}

bool decimal::operator<=(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1))
    return !(rhs < *this);
  if (a.is_nan() || b.is_nan()) return false;
  return !(rhs < *this);
}

bool decimal::operator>(const decimal& rhs) const { return rhs < *this; }

bool decimal::operator>=(const decimal& rhs) const {
  auto& a = decimalImpl::of(*this);
  auto& b = decimalImpl::of(rhs);
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1))
    return !(*this < rhs);
  if (a.is_nan() || b.is_nan()) return false;
  return !(*this < rhs);
}

// ---------------------------------------------------------------------------
// Stream output
// ---------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const decimal& d) {
  return os << d.to_string();
}

}  // namespace decimals

// ---------------------------------------------------------------------------
// std::numeric_limits<decimals::decimal>
// ---------------------------------------------------------------------------

decimals::decimal std::numeric_limits<decimals::decimal>::min() noexcept {
  return decimals::make_with_fields(decimals::POSITIVE,
                                    decimals::kMinExponent + 18, 1);
}

decimals::decimal std::numeric_limits<decimals::decimal>::max() noexcept {
  return decimals::make_with_fields(decimals::POSITIVE, decimals::kMaxExponent,
                                    9999999999999999999ULL);
}

decimals::decimal std::numeric_limits<decimals::decimal>::lowest() noexcept {
  return decimals::make_with_fields(decimals::NEGATIVE, decimals::kMaxExponent,
                                    9999999999999999999ULL);
}

decimals::decimal std::numeric_limits<decimals::decimal>::epsilon() noexcept {
  return decimals::make_with_fields(decimals::POSITIVE, -18, 1);
}

decimals::decimal
std::numeric_limits<decimals::decimal>::round_error() noexcept {
  return decimals::make_with_fields(decimals::POSITIVE, -1, 5);  // 0.5
}

decimals::decimal std::numeric_limits<decimals::decimal>::infinity() noexcept {
  return decimals::decimal::inf();
}

decimals::decimal
std::numeric_limits<decimals::decimal>::quiet_NaN() noexcept {
  return decimals::decimal::nan();
}

decimals::decimal
std::numeric_limits<decimals::decimal>::signaling_NaN() noexcept {
  return decimals::decimal::nan();
}

decimals::decimal
std::numeric_limits<decimals::decimal>::denorm_min() noexcept {
  return min();
}
