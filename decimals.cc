#include "decimals.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <ostream>
#include <utility>

namespace decimals {

// ---------------------------------------------------------------------------
// Fast-path optimization strategy
//
// All operators check whether both operands are non-special (i.e. neither
// inf nor NaN) before doing anything else.  This single test — a comparison
// of the raw exponent field against SPECIAL_EXP — covers the common case
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

namespace {  // all private symbols belong here

// ---------------------------------------------------------------------------
// Constants (internal to this TU)
// ---------------------------------------------------------------------------

constexpr uint64_t EXPONENT_MASK = (1ULL << 63) - 1;
constexpr int64_t EXPONENT_BIAS =
    static_cast<int64_t>((EXPONENT_MASK - 1) / 2);  // 4611686018427387903
constexpr uint64_t SIGN_BIT = 1ULL << 63;
constexpr uint64_t SPECIAL_EXP = EXPONENT_MASK;
constexpr int64_t MAX_EXPONENT = EXPONENT_BIAS;
constexpr int64_t MIN_EXPONENT = -EXPONENT_BIAS;

// Power-of-5 lookup table: POW5[i] = 5^i for i in [0, 27].
// 5^27 = 7.45e18 < 2^63; 5^28 > UINT64_MAX.
constexpr uint64_t POW5[28] = {
    1ULL,
    5ULL,
    25ULL,
    125ULL,
    625ULL,
    3125ULL,
    15625ULL,
    78125ULL,
    390625ULL,
    1953125ULL,
    9765625ULL,
    48828125ULL,
    244140625ULL,
    1220703125ULL,
    6103515625ULL,
    30517578125ULL,
    152587890625ULL,
    762939453125ULL,
    3814697265625ULL,
    19073486328125ULL,
    95367431640625ULL,
    476837158203125ULL,
    2384185791015625ULL,
    11920928955078125ULL,
    59604644775390625ULL,
    298023223876953125ULL,
    1490116119384765625ULL,
    7450580596923828125ULL,
};

// Mantissa normalization range: [10^18, 10^19).

// Power-of-10 lookup table: POW10[i] = 10^i for i in [0, 19].
constexpr uint64_t POW10[20] = {
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
int_fast32_t count_digits_u64(uint64_t v) {
  // floor(log2(v)) via clzll; 1233/4096 ≈ log10(2) (slight underestimate).
  // d = floor(log10(v)) or floor(log10(v))-1; one table lookup corrects it.
  const int_fast32_t bits = 63 - __builtin_clzll(v | 1);
  const int_fast32_t d = (bits * 1233) >> 12;
  return d + 1 + (v >= POW10[d + 1]);
}

// to_string() uses plain decimal when the number of digits before the
// decimal point (dp) is in [-PLAIN_DP_LIMIT, PLAIN_DP_LIMIT].  Beyond that,
// scientific notation keeps the output within decimal::max_string_length.
constexpr int64_t PLAIN_DP_LIMIT = 20;

// Decompose |d| into integer significand f and exponent e such that
// |d| = f * 2^e exactly.  For normal doubles f is in [2^52, 2^53).
inline std::pair<uint64_t, int_fast32_t> decompose_double(double d) {
  union { double d; uint64_t ui; } bits = {d};
  bits.ui &= ~(1ULL << 63);
  uint64_t f = bits.ui & ((1ULL << 52) - 1);
  int_fast32_t e = static_cast<int>(bits.ui >> 52);
  if (e != 0) {
    f |= 1ULL << 52;  // normal: restore implicit leading 1
    e -= 1023 + 52;
  } else {
    e = 1 - 1023 - 52;  // subnormal: e = -1074
  }
  return {f, e};
}

// ---------------------------------------------------------------------------
// decimal_impl — inherits from decimal to access the protected x[2] storage
// and adds private helpers used by the implementation.
// ---------------------------------------------------------------------------

class decimal_impl : public decimal {
 public:
  decimal_impl(uint64_t x0, uint64_t x1) { x[0] = x0; x[1] = x1; }

  static std::pair<uint64_t, int64_t> fit_to_uint64(__uint128_t value, int64_t exponent);

  static decimal make(sign s, int64_t exponent, uint64_t mantissa);
  static decimal make(sign s, std::pair<uint64_t, int64_t> me) { return make(s, me.second, me.first); }
  static decimal make_special(sign s, uint64_t mantissa);

  static decimal add_magnitudes(const decimal& a, const decimal& b, sign rs);
  static decimal sub_magnitudes(const decimal& larger, const decimal& smaller,
                                sign rs);

  // Core implementation shared by operator+ (b_sign_xor=0) and
  // operator- (b_sign_xor=SIGN_BIT).  The effective sign of b is
  // b.get_sign() XOR'd with b_sign_xor before any comparison.
  static inline decimal add_impl(const decimal_impl& a, const decimal_impl& b,
                                 uint64_t b_sign_xor);
  static sign mul_sign(const decimal_impl& a, const decimal_impl& b) {
    static_assert(!((bool)(POSITIVE) && (bool)(NEGATIVE)), "the expression in the return below won't work");
    return (sign) ((a.x[0] ^ b.x[0]) & SIGN_BIT);
  }

  sign get_sign() const { return (sign)(x[0] & SIGN_BIT); }

  uint64_t raw_exponent() const { return x[0] & EXPONENT_MASK; }

  int64_t biased_exponent() const {
    return static_cast<int64_t>(raw_exponent()) - EXPONENT_BIAS;
  }
};

static_assert(sizeof(decimal_impl) == sizeof(decimal), "decimal_impl and decimal must have the exact same members");

std::pair<uint64_t, int64_t> decimal_impl::fit_to_uint64(__uint128_t value, int64_t exponent) {
  uint_fast64_t hi = static_cast<uint_fast64_t>(value >> 64);
  if (hi == 0) return {static_cast<uint64_t>(value), exponent};

  // Estimate k = decimal digits to strip so value/10^k fits in uint64_t.
  // bit_len(value) = 128 - clz(hi); hi != 0 since value > UINT64_MAX.
  // floor(bit_len * log10(2)) ≈ floor(log10(value)); 1233/4096 ≈ log10(2).
  int64_t k = std::max(1, (((128 - __builtin_clzll(hi)) * 1233) >> 12) - 19);

  const uint_fast64_t divisor = POW10[k];
  __uint128_t q = value / divisor;

  bool round_up;
  if (q <= static_cast<__uint128_t>(UINT64_MAX)) {
    // Round half-up: 2*rem >= divisor, computed as rem >= divisor-rem to avoid overflow.
    const uint64_t rem = static_cast<uint64_t>(value - q * divisor);
    round_up = rem >= divisor - rem;
  } else {
    // k undershot by 1; strip one more digit.
    const __uint128_t qtenth = q / 10;
    round_up = static_cast<uint_fast64_t>(q - 10 * qtenth) >= 5;
    q = qtenth;
    ++k;
  }

  uint64_t q64 = static_cast<uint64_t>(q);
  if (round_up && q64 < UINT64_MAX) ++q64;

  return std::pair<uint64_t, int64_t>(q64, exponent + k);
}

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------

decimal decimal_impl::make_special(sign s, uint64_t mantissa) {
  return decimal_impl(s | SPECIAL_EXP, mantissa);
}

decimal decimal_impl::make(sign s, int64_t exponent, uint64_t mantissa) {
  if (mantissa == 0) return decimal::zero(s);

  const int_fast32_t deficit = 19 - count_digits_u64(mantissa);

  exponent -= std::max(deficit, (int_fast32_t)-1);

  if (exponent > MAX_EXPONENT) return decimal::inf(s);
  if (exponent < MIN_EXPONENT) return decimal::zero(s);

  if (deficit > 0) {
    // Pad mantissa up to 19 digits in one multiply.
    mantissa *= POW10[deficit];
  } else if (deficit < 0) {
    // 20 digits (max for uint64_t).  Divide by 10 and round.
    // GCC and Clang recognize (mantissa / 10) and (mantissa % 10) as a pair
    // and emit a single multiply-shift sequence for both.
    mantissa = mantissa / 10 + (mantissa % 10 >= 5);
    // A rounding carry (next + 1 == 10^19) would require
    // next >= 9999999999999999999 ≈ 10^19, which in turn requires
    // mantissa >= 99999999999999999990, impossible for uint64_t
    // (max ≈ 1.84 × 10^19).  So next + 1 < 10^19 is always true here.
  }

  return decimal_impl(((uint64_t)(exponent + EXPONENT_BIAS)) | s, mantissa);
}

// ---------------------------------------------------------------------------
// Arithmetic helpers
// ---------------------------------------------------------------------------

// Precondition: both a and b are non-zero finite decimals with the same sign.
decimal decimal_impl::add_magnitudes(const decimal& a, const decimal& b,
                                     sign rs) {
  auto& ai = static_cast<const decimal_impl&>(a);
  auto& bi = static_cast<const decimal_impl&>(b);
  uint64_t ma = ai.mantissa(), mb = bi.mantissa();
  int64_t ea = ai.biased_exponent(), eb = bi.biased_exponent();

  if (ea < eb) {
    std::swap(ea, eb);
    std::swap(ma, mb);
  }

  int64_t diff = ea - eb;
  if (diff >= 20) return make(rs, ea, ma);

  return make(rs, fit_to_uint64(static_cast<__uint128_t>(ma) * POW10[diff] + mb, eb));
}

// Precondition: |larger| >= |smaller| (guaranteed by compare_abs at the call
// site), which implies larger.biased_exponent() >= smaller.biased_exponent().
// The ea < eb branch that would contradict this invariant has been removed.
decimal decimal_impl::sub_magnitudes(const decimal& larger, const decimal& smaller, sign rs) {
  auto& li = static_cast<const decimal_impl&>(larger);
  auto& si = static_cast<const decimal_impl&>(smaller);
  uint64_t ma = li.mantissa(), mb = si.mantissa();
  int64_t ea = li.biased_exponent(), eb = si.biased_exponent();

  if (mb == 0) return make(rs, ea, ma);

  // ea >= eb is guaranteed by the precondition (see above).
  const int64_t diff = ea - eb;
  return (diff >= 20) ? make(rs, ea, ma) : make(rs, fit_to_uint64(static_cast<__uint128_t>(ma) * POW10[diff] - static_cast<__uint128_t>(mb), eb));
}

inline decimal decimal_impl::add_impl(const decimal_impl& a, const decimal_impl& b, uint64_t b_sign_xor) {
  // Fast path: both finite (including zero).
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    const uint64_t ma = a.mantissa(), mb = b.mantissa();

    // Zero checks before sign extraction: mb==0 just returns a (no signs
    // needed), and ma==0 with non-zero b only needs b's effective x[0].
    if (mb == 0) {
      if (ma == 0)
        return zero((sign)(a.x[0] & (b.x[0] ^ b_sign_xor) & SIGN_BIT));
      return a;
    }
    if (ma == 0) return decimal_impl(b.x[0] ^ b_sign_xor, mb);

    if (!((a.x[0] ^ b.x[0] ^ b_sign_xor) & SIGN_BIT)) {
      return add_magnitudes(a, b, (sign)(a.x[0] & SIGN_BIT));
    }

    const auto bea = a.biased_exponent(), beb = b.biased_exponent();
    if (bea > beb) {
      return sub_magnitudes(a, b, (sign)(a.x[0] & SIGN_BIT));
    } else if (beb > bea || mb > ma) {
      return sub_magnitudes(b, a, (sign)((b.x[0] ^ b_sign_xor) & SIGN_BIT));
    }
    if (ma > mb) return sub_magnitudes(a, b, (sign)(a.x[0] & SIGN_BIT));
    return zero();
  }

  // Slow path: at least one inf or NaN.
  if (a.is_special()) {
    if (a.x[1] || !b.is_special()) return a;
    // a is inf and b is special
    if (b.x[1]) return nan();
    // both are inf
    return ((a.x[0] ^ b.x[0] ^ b_sign_xor) & SIGN_BIT) ? nan() : static_cast<const decimal&>(a);
  }
  // b is special and a isn't
  return b.x[1] ? nan() : decimal_impl(b.x[0] ^ b_sign_xor, b.mantissa());
}

class hex_float_parser {
 public:
  decimal parse(sign s, const unsigned char* p, const char** endptr) {
    const unsigned char* after_0s = p += 2;  // skip "0x"/"0X"
    while (__builtin_expect(*after_0s == '0', 0)) ++after_0s;

    const unsigned char* q = parse_hex_mantissa(after_0s);
    if (__builtin_expect(after_0s == p && (q == after_0s || (q <= after_0s + 1 && *p == '.')), 0)) {
      // "0x" with no hex digits: consumed just the '0'
      if (endptr) *endptr = (const char*)(p - 1);
      return decimal::zero(s);
    }

    // Parse 'p'/'P' exponent
    auto [q2, bin_exp] = parse_bin_exponent(q);
    q = q2;

    if (endptr) *endptr = (const char*)q;
    if (mant == 0) return decimal::zero(s);

    // value = mant * 2^(bin_exp + bit_adj)
    const int64_t total_exp = bin_exp + bit_adj;

    // Early out for extreme exponents (mant <= 2^64, so double
    // overflows above ~2^1024 and underflows below ~2^-1138).
    if (__builtin_expect(total_exp > 1100, 0)) return decimal::inf(s);
    if (__builtin_expect(total_exp < -1200, 0)) return decimal::zero(s);

    const double val = std::ldexp(static_cast<double>(mant), static_cast<int>(total_exp));
    return (s == POSITIVE) ? decimal::from_double(val) : -decimal::from_double(val);
  }

 private:
  template <bool overflowing=false> const unsigned char* parse_hex_mantissa(const unsigned char* q, int_fast32_t sig_count = 0) {
    for (;;) {
      const uint_fast8_t c = *q;
      uint_fast8_t nib = c - '0';
      if (__builtin_expect(nib > 9, 0)) {
        nib = (c | 0x20) - 'a';
        if (nib <= 'f' - 'a') nib += 10; else return (c == '.') ? parse_fractional_digits<overflowing>(q + 1, sig_count) : q;
      }
      if constexpr (!overflowing) {
        mant = (mant << 4) | static_cast<uint64_t>(nib);
        if (++sig_count == 16) return parse_hex_mantissa<true>(q + 1);
      } else {
        bit_adj += 4;
      }
      ++q;
    }
  }

  static std::pair<const unsigned char*, int64_t> parse_unsigned_exp(const unsigned char* pq) {
    uint_fast8_t i = static_cast<uint_fast8_t>(*pq) - '0';
    if (i <= 9) {
      int64_t bin_exp = 0;
      do {
        bin_exp = (bin_exp < 5000000000LL) ? bin_exp * 10 + i : 5000000000LL;
        i = static_cast<uint_fast8_t>(*++pq) - '0';
      } while (i <= 9);
      return {pq, bin_exp};
    }
    return {pq, 0};
  }

  static std::pair<const unsigned char*, int64_t> parse_bin_exponent(const unsigned char* q) {
    if ((*q | 0x20) != 'p') return {q, 0};
    const unsigned char* pq = q + 1;
    if (*pq == '-') {
      auto [end, val] = parse_unsigned_exp(pq + 1);
      return (end == pq + 1) ? std::pair(q, (int64_t)0) : std::pair(end, -val);
    } else {
      if (__builtin_expect(*pq == '+', 0)) ++pq;
      auto [end, val] = parse_unsigned_exp(pq);
      return (end == pq) ? std::pair(q, (int64_t)0) : std::pair(end, val);
    }
  }

  template <bool overflowing> const unsigned char* parse_fractional_digits(const unsigned char* q, int_fast32_t sig_count) {
    for (;;) {
      const uint_fast8_t c = *q;
      uint_fast8_t nib = c - '0';
      if (__builtin_expect(nib > 9, 0)) {
        if ((nib = (c | 0x20) - 'a') <= 'f' - 'a') nib += 10; else return q;
      }
      if constexpr (!overflowing) {
        if (mant == 0 && nib == 0) { bit_adj -= 4; ++q; continue; }
        mant = (mant << 4) | static_cast<uint64_t>(nib);
        bit_adj -= 4;
        if (++sig_count == 16) return parse_fractional_digits<true>(q + 1, sig_count);
      }
      ++q;
    }
  }

  uint64_t mant = 0;
  int_fast32_t bit_adj = 0; // net bit adjustment: +4 per overflow int digit, -4 per fractional digit
};

class basic_positive_representation_parser {
 public:
  decimal parse(sign s, const unsigned char* p, const char* orig, const char** endptr) {
    const unsigned char* q = p;

    // read the number before 'e'
    while (__builtin_expect(*p == '0', 0)) ++p;
    p = parse_mantissa(p);
    if (__builtin_expect(p <= q + 1, 0) && (p == q || *q == '.')) {
      // we didn't see any digits
      if (endptr) *endptr = orig;
      return decimal::zero();
    }
    if (mant == 0) {
      if (__builtin_expect((*p | 0x20) == 'e', 0)) {
        q = p++;
        p += *p == '-' || *p == '+';
        if (std::isdigit(*p)) {
          do { ++p; } while (std::isdigit(*p));
        } else {
          if (endptr) *endptr = (const char*)q;
	  return decimal::zero(s);
        }
      }
      if (endptr) *endptr = (const char*)p;
      return decimal::zero(s);
    }

    if (__builtin_expect((*p | 0x20) != 'e', 1)) {
      e_exp = 0;
    } else {
      q = p;
      if (*++p != '-') {
        p = parse_exp_uint(p + (*p == '+'), q);
      } else {
        p = parse_exp_uint(p + 1, q);
        e_exp = -e_exp;
      }
    }

    if (endptr) *endptr = (const char*)p;
    return decimal_impl::make(s, e_exp + extra_digits, mant);
  }

 private:
  const unsigned char* parse_exp_uint(const unsigned char* p, const unsigned char* result_if_not_uint) {
    if (__builtin_expect(!std::isdigit(*p), 0)) {
      e_exp = 0;
      return result_if_not_uint;
    } else {
      e_exp = 0;
      do {
        // Saturate at 5e18 (> MAX_EXPONENT) to avoid int64_t overflow.
         e_exp = (e_exp < 500000000000000000LL) ? e_exp * 10 + (*p - '0') : 5000000000000000000LL;
      } while (std::isdigit(* ++p));
      return p;
    }
  }

  template <bool before_dot=true, int dropping=0> const unsigned char* parse_mantissa(const unsigned char* p, uint_fast16_t mant_digits=0) {
    while (*p) {
      if (__builtin_expect(!std::isdigit(*p), 0)) {
        if (before_dot && *p == '.') return parse_mantissa<false, dropping>(p + 1, mant_digits);
        break;
      }
      if constexpr (!before_dot) --extra_digits;
      static_assert(dropping >= 0 && dropping <= 2, "the value of dropping isn't known at compile time -> the compiler won't be able to optimize the switch/case");
      switch (dropping) {
      case 0:  // before the dot
        mant = mant * 10 + static_cast<uint64_t>(*p - '0');
        if (++mant_digits >= 19) return parse_mantissa<before_dot, 1>(p + 1, mant_digits);
        break;
      case 1:  // after the dot
        mant += (*p >= '5');
        ++extra_digits;
        return parse_mantissa<before_dot, 2>(p + 1, mant_digits);
      case 2:  // past the too many digits point (can be before or after dot)
        ++extra_digits;
        break;
      }
      ++p;
    }
    return p;
  }

  uint64_t mant = 0;
  int64_t extra_digits = 0;
  int64_t e_exp;
};

}  // namespace

// ---------------------------------------------------------------------------
// decimal public methods
// ---------------------------------------------------------------------------

decimal::decimal(double d) { *this = from_double(d); }

decimal::decimal(int64_t v) {
  if (v == 0) { x[0] = 0; x[1] = 0; return; }
  uint64_t sign_bit;
  uint64_t u;
  if (v > 0) {
    sign_bit = 0;
    u = static_cast<uint64_t>(v);
  } else {
    sign_bit = SIGN_BIT;
    // Safe even for INT64_MIN: -(int64_t)INT64_MIN is UB, but the
    // unsigned cast is well-defined and produces the right magnitude.
    u = static_cast<uint64_t>(-static_cast<__int128>(v));
  }
  // int64_t magnitude < 10^19, so deficit = 19 - digits(u) >= 0 always.
  // Exponent starts at 0, ends at -deficit; no overflow/underflow possible.
  // v == 0 is handled correctly: x[1] = 0 (valid zero, non-canonical exponent).
  const int_fast32_t deficit = 19 - count_digits_u64(u);
  x[1] = u * POW10[deficit];
  x[0] = static_cast<uint64_t>(EXPONENT_BIAS - deficit) | sign_bit;
}

decimal::decimal(uint64_t v) {
  // uint64_t fits in at most 20 digits, so deficit >= -1.
  // Exponent ends at -deficit in [-1, 18]; no overflow/underflow possible.
  // v == 0 is handled correctly: x[1] = 0 (valid zero, non-canonical exponent).
  const int_fast32_t deficit = 19 - count_digits_u64(v);
  if (deficit >= 0) {
    x[1] = v * POW10[deficit];
    x[0] = static_cast<uint64_t>(EXPONENT_BIAS - deficit);
  } else {
    // deficit == -1: 20-digit value, round off last digit.
    x[1] = v / 10 + (v % 10 >= 5);
    x[0] = static_cast<uint64_t>(EXPONENT_BIAS + 1);
  }
}

decimal decimal::nan() { return decimal_impl::make_special(POSITIVE, 1); }

decimal decimal::inf(sign s) { return decimal_impl::make_special(s, 0); }

decimal decimal::zero(sign s) { return decimal_impl(s, 0); }

decimal decimal::from_double(double d) {
  if (__builtin_expect(!std::isfinite(d), 0)) {
    if (std::isnan(d)) return nan();
    return inf(d < 0 ? NEGATIVE : POSITIVE);
  }
  if (d == 0.0) return zero(std::signbit(d) ? NEGATIVE : POSITIVE);

  // Decompose |d| = f * 2^e exactly (IEEE 754 binary64).
  auto [f, e] = decompose_double(d);

  // Convert f * 2^e to m * 10^exp10 using 128-bit integer arithmetic.
  int64_t exp10 = 0;
  uint64_t m;

  if (e >= 0) {
    // d = f * 2^e (exact integer); iteratively shift left, trimming via
    // fit_to_uint64 to keep the intermediate result in uint64_t.
    m = f;
    while (e > 0) {
      const int_fast32_t batch = e > 63 ? 63 : e;
      e -= batch;
      std::tie(m, exp10) = decimal_impl::fit_to_uint64(static_cast<__uint128_t>(m) << batch, exp10);
    }
  } else {
    // d = f / 2^(-e) = f * 5^(-e) / 10^(-e) = f * 5^(-e) * 10^e.
    // Iteratively multiply by powers of 5, trimming to keep m in uint64_t.
    exp10 = e;
    m = f;
    int_fast32_t remaining = -e;
    while (remaining > 0) {
      const int_fast32_t batch = remaining > 27 ? 27 : remaining;
      remaining -= batch;
      std::tie(m, exp10) = decimal_impl::fit_to_uint64(static_cast<__uint128_t>(m) * POW5[batch], exp10);
    }
  }

  return decimal_impl::make(std::signbit(d) ? NEGATIVE : POSITIVE, exp10, m);
}

// ---------------------------------------------------------------------------
// Accessors / queries
// ---------------------------------------------------------------------------

bool decimal::is_negative() const { return x[0] & SIGN_BIT; }

int64_t decimal::exponent() const { return static_cast<const decimal_impl*>(this)->biased_exponent(); }

uint64_t decimal::mantissa() const { return x[1]; }

bool decimal::is_special() const { return (x[0] & EXPONENT_MASK) == SPECIAL_EXP; }
bool decimal::is_nan() const { return (x[0] & EXPONENT_MASK) == SPECIAL_EXP && x[1] != 0; }
bool decimal::is_inf() const { return (x[0] & EXPONENT_MASK) == SPECIAL_EXP && x[1] == 0; }
bool decimal::is_zero() const { return (x[0] & EXPONENT_MASK) != SPECIAL_EXP && x[1] == 0; }
bool decimal::is_finite() const { return !is_special(); }

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

double decimal::to_double() const {
  if (__builtin_expect(is_special(), 0)) {
    if (mantissa() != 0) return std::numeric_limits<double>::quiet_NaN();
    return is_negative() ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
  }
  uint64_t m = mantissa();
  if (m == 0) return (x[0] & SIGN_BIT) ? -0.0 : 0.0;

  // value = m * 10^exp10 = m * 5^exp10 * 2^exp10.
  // Compute m' ≈ m * 5^exp10 (or m / 5^|exp10|) keeping ~64 bits of
  // precision, accumulating binary exponent corrections in bin_exp.
  // Result: ldexp((double)m', bin_exp + exp10).
  const int64_t exp10 = static_cast<const decimal_impl*>(this)->biased_exponent();

  // Early exit for extreme exponents (m is in [10^18, 10^19)).
  // Cast to uint64_t collapses both bounds into one test: exp10 < -363 wraps
  // to a huge unsigned value, exp10 > 309 exceeds 672; normal range maps to
  // [0, 672].
  if (__builtin_expect((uint64_t)(exp10 + 363) > 672u, 0)) {
    if (exp10 > 309)
      return (x[0] & SIGN_BIT) ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    return (x[0] & SIGN_BIT) ? -0.0 : 0.0;
  }

  int_fast64_t bin_exp = 0;

  if (exp10 > 0) {
    // Multiply m by 5^exp10 iteratively, trimming to 64 bits.
    int_fast32_t remaining = static_cast<int_fast32_t>(exp10);
    while (remaining > 0) {
      const int_fast32_t batch = remaining > 27 ? 27 : remaining;
      const __uint128_t product = static_cast<__uint128_t>(m) * POW5[batch];
      remaining -= batch;
      const uint64_t hi = static_cast<uint64_t>(product >> 64);
      if (hi != 0) {
        const int_fast32_t shift = 64 - __builtin_clzll(hi);
        m = static_cast<uint64_t>(product >> shift);
        bin_exp += shift;
      } else {
        m = static_cast<uint64_t>(product);
      }
    }
  } else if (exp10 < 0) {
    // Divide m by 5^|exp10| iteratively, shifting left first for precision.
    int_fast32_t remaining = static_cast<int_fast32_t>(-exp10);
    while (remaining > 0) {
      const int_fast32_t batch = remaining > 27 ? 27 : remaining;
      const int_fast32_t shift = __builtin_clzll(m) + 64;
      bin_exp -= shift;
      const __uint128_t quotient = (static_cast<__uint128_t>(m) << shift) / POW5[batch];
      remaining -= batch;
      const uint64_t qhi = static_cast<uint64_t>(quotient >> 64);
      if (qhi != 0) {
        const int_fast32_t excess = 64 - __builtin_clzll(qhi);
        m = static_cast<uint64_t>(quotient >> excess);
        bin_exp += excess;
      } else {
        m = static_cast<uint64_t>(quotient);
      }
    }
  }

  const double result = std::ldexp(static_cast<double>(m), static_cast<int>(bin_exp + exp10));
  return is_negative() ? -result : result;
}

decimal::operator bool() const { return !is_zero(); }

decimal::operator double() const { return to_double(); }

decimal::operator int64_t() const {
  if (__builtin_expect(is_special(), 0)) {
    if (is_nan()) return 0;
    return (x[0] & SIGN_BIT) ? INT64_MIN : INT64_MAX;
  }
  const uint64_t m = mantissa();
  if (m == 0) return 0;

  const int64_t exp = static_cast<const decimal_impl*>(this)->biased_exponent();

  if (exp < -18) return 0;  // |value| < 1
  if (exp > 0) {
    // m >= 10^18, m * 10 >= 10^19 > INT64_MAX
    return (x[0] & SIGN_BIT) ? INT64_MIN : INT64_MAX;
  }

  // exp in [-18, 0]: value = m / 10^(-exp)
  const uint64_t abs_val = (exp == 0) ? m : m / POW10[static_cast<int>(-exp)];

  if (x[0] & SIGN_BIT) {
    if (abs_val >= 9223372036854775808ULL) return INT64_MIN;
    return -static_cast<int64_t>(abs_val);
  }
  if (abs_val > 9223372036854775807ULL) return INT64_MAX;
  return static_cast<int64_t>(abs_val);
}

decimal::operator uint64_t() const {
  if (__builtin_expect(is_special(), 0)) {
    if (is_nan()) return 0;
    return (x[0] & SIGN_BIT) ? 0ULL : UINT64_MAX;
  }
  const uint64_t m = mantissa();
  if (m == 0 || (x[0] & SIGN_BIT)) return 0;

  const int64_t exp = static_cast<const decimal_impl*>(this)->biased_exponent();

  if (__builtin_expect(exp < -18, 0)) return 0;
  if (__builtin_expect(exp >= 2, 0)) return UINT64_MAX;
  if (exp == 1) {
    // m * 10, check overflow
    if (__builtin_expect(m > UINT64_MAX / 10, 0)) return UINT64_MAX;
    return m * 10;
  }
  if (exp == 0) return m;

  // exp in [-18, -1]
  return m / POW10[-exp];
}

decimal decimal::mul_pow10(int64_t p) const {
  if (__builtin_expect(is_special() || mantissa() == 0, 0)) return *this;
  const auto& impl = static_cast<const decimal_impl&>(*this);
  const auto new_raw_exp = static_cast<__int128>(impl.raw_exponent()) + p;
  if (new_raw_exp > 2 * EXPONENT_BIAS) return decimal::inf(impl.get_sign());
  if (new_raw_exp < 0) return decimal::zero(impl.get_sign());
  return decimal_impl((x[0] & ~EXPONENT_MASK) | static_cast<uint64_t>(new_raw_exp), x[1]);
}

decimal decimal::pow10(int64_t p) {
  // pow10(p) = 10^18 * 10^(p-18); mantissa is always 10^18.
  if (__builtin_expect(p > MAX_EXPONENT + 18, 0)) return inf(POSITIVE);
  if (__builtin_expect(p < MIN_EXPONENT + 18, 0)) return zero(POSITIVE);
  return decimal_impl(static_cast<uint64_t>(p - 18 + EXPONENT_BIAS), POW10[18]);
}

int64_t decimal::ilog10() const {
  if (__builtin_expect(is_special() || mantissa() == 0, 0)) return 0;
  // mantissa is in [10^18, 10^19), so floor(log10(mantissa)) = 18 always.
  return static_cast<const decimal_impl*>(this)->biased_exponent() + 18;
}

// ---------------------------------------------------------------------------
// Scientific-notation formatter: [-]d[.ddd...]e[-]ddd...
// ---------------------------------------------------------------------------

// Formats d into buf[64] right-to-left.
// Returns pointer to start of formatted string; buf + 64 is the end.
static const char* format_decimal(const decimal& d, char (&buf)[64]) {
  const auto& impl = static_cast<const decimal_impl&>(d);
  char* const end = buf + sizeof(buf);
  char* p = end;

  uint64_t m = d.mantissa();
  if (__builtin_expect(!impl.is_special() && m != 0, 1)) {
    int64_t exp = impl.biased_exponent();
    int64_t n = 19;
    while (m % 10 == 0) { m /= 10; ++exp; --n; }

    const int64_t dp = n + exp;

    if (dp > PLAIN_DP_LIMIT || dp < -PLAIN_DP_LIMIT) {
      // Scientific notation: [-]d[.ddd...]e[+-]exp
      const int64_t sci_exp = dp - 1;
      const bool neg_exp = sci_exp < 0;
      uint64_t etmp = static_cast<uint64_t>(neg_exp ? -sci_exp : sci_exp);
      do { *--p = static_cast<char>('0' + etmp % 10); etmp /= 10; } while (etmp);
      if (neg_exp) *--p = '-';
      *--p = 'e';
      uint64_t tmp = m;
      for (int64_t i = 0; i < n - 1; ++i) { *--p = static_cast<char>('0' + tmp % 10); tmp /= 10; }
      if (n > 1) *--p = '.';
      *--p = static_cast<char>('0' + tmp);  // most-significant digit
    } else if (dp >= n) {
      // Integer: ddd[000...]
      for (int64_t i = 0; i < dp - n; ++i) *--p = '0';
      uint64_t tmp = m;
      while (tmp) { *--p = static_cast<char>('0' + tmp % 10); tmp /= 10; }
    } else if (dp > 0) {
      // Decimal point within digits: ddd.ddd
      uint64_t tmp = m;
      for (int64_t i = 0; i < n - dp; ++i) { *--p = static_cast<char>('0' + tmp % 10); tmp /= 10; }
      *--p = '.';
      while (tmp) { *--p = static_cast<char>('0' + tmp % 10); tmp /= 10; }
    } else {
      // Pure fraction: 0.[000...]ddd
      uint64_t tmp = m;
      while (tmp) { *--p = static_cast<char>('0' + tmp % 10); tmp /= 10; }
      for (int64_t i = 0; i < -dp; ++i) *--p = '0';
      *--p = '.'; *--p = '0';
    }
    if (impl.get_sign() == NEGATIVE) *--p = '-';
    return p;
  }

  // Slow path: special values and zero.
  if (__builtin_expect(impl.is_special(), 0)) {
    if (m) { p -= 3; p[0] = 'n'; p[1] = 'a'; p[2] = 'n'; }
    else if (impl.get_sign() == NEGATIVE) { p -= 4; p[0] = '-'; p[1] = 'i'; p[2] = 'n'; p[3] = 'f'; }
    else { p -= 3; p[0] = 'i'; p[1] = 'n'; p[2] = 'f'; }
    return p;
  }
  if (impl.get_sign() == NEGATIVE) { p -= 2; p[0] = '-'; p[1] = '0'; }
  else { --p; p[0] = '0'; }
  return p;
}

std::string decimal::to_string() const {
  char buf[64];
  const char* p = format_decimal(*this, buf);
  return {p, static_cast<size_t>(buf + 64 - p)};
}

char* decimal::to_string(char* buf) const {
  char tmp[64];
  const char* p = format_decimal(*this, tmp);
  size_t len = static_cast<size_t>(tmp + 64 - p);
  std::memcpy(buf, p, len);
  buf[len] = '\0';
  return buf;
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

  while (std::isspace(static_cast<unsigned char>(*p))) ++p;

  sign s = POSITIVE;
  if (__builtin_expect(*p == '-', 0)) { s = NEGATIVE; ++p; } else if (*p == '+') { ++p; }

  if (__builtin_expect(std::isdigit(static_cast<unsigned char>(*p)), 1)) {
    if (__builtin_expect((p[1] | 0x20) == 'x' && p[0] == '0', 0))
      return hex_float_parser().parse(s, (const unsigned char*)p, endptr);
    return basic_positive_representation_parser().parse(s, (const unsigned char*)p, orig, endptr);
  }

  if (*p == '.') return basic_positive_representation_parser().parse(s, (const unsigned char*)p, orig, endptr);

  // NaN (case-insensitive).
  if (((p[0] | 0x20) == 'n') && ((p[1] | 0x20) == 'a') && ((p[2] | 0x20) == 'n')) {
    if (endptr) *endptr = p + 3;
    return nan();
  }

  // Infinity (case-insensitive).
  if ((p[0] | 0x20) == 'i' && (p[1] | 0x20) == 'n' && (p[2] | 0x20) == 'f') {
    if (!endptr) return inf(s);
    p += 3;
    if ((p[0] | 0x20) == 'i' && (p[1] | 0x20) == 'n' && (p[2] | 0x20) == 'i' && (p[3] | 0x20) == 't' && (p[4] | 0x20) == 'y') {
      p += 5;
    }
    *endptr = p;
    return inf(s);
  }

  if (endptr) *endptr = orig;
  return decimal::zero();
}

// ---------------------------------------------------------------------------
// Arithmetic operators
// ---------------------------------------------------------------------------

decimal& decimal::flip_sign() { x[0] ^= SIGN_BIT; return *this; }

decimal decimal::operator-() const {
  decimal d = *this;
  d.flip_sign();
  return d;
}

decimal decimal::abs() const { return decimal_impl(x[0] & ~SIGN_BIT, x[1]); }

decimal decimal::operator+(const decimal& rhs) const {
  return decimal_impl::add_impl(static_cast<const decimal_impl&>(*this),
                                static_cast<const decimal_impl&>(rhs), 0);
}

decimal decimal::operator-(const decimal& rhs) const {
  return decimal_impl::add_impl(static_cast<const decimal_impl&>(*this),
                                static_cast<const decimal_impl&>(rhs),
                                SIGN_BIT);
}

decimal decimal::operator*(const decimal& rhs) const {
  const auto& a = static_cast<const decimal_impl&>(*this);
  const auto& b = static_cast<const decimal_impl&>(rhs);

  // Fast path: both finite.
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    const sign rs = decimal_impl::mul_sign(a, b);
    const uint64_t ma = a.mantissa();
    if (ma == 0) return zero(rs);
    const uint64_t mb = b.mantissa();
    if (mb == 0) return zero(rs);

    return decimal_impl::make(rs, decimal_impl::fit_to_uint64(static_cast<__uint128_t>(ma) * mb, a.biased_exponent() + b.biased_exponent()));
  }

  // Slow path: at least one inf or NaN.
  if (a.is_special()) {
    if (a.x[1] || b.is_zero() || b.is_nan()) return nan();
    return inf(decimal_impl::mul_sign(a, b));
  }
  // b is special and a isn't
  if (b.x[1] || a.is_zero()) return nan();
  return inf(decimal_impl::mul_sign(a, b));
}

decimal decimal::operator/(const decimal& rhs) const {
  const auto& a = static_cast<const decimal_impl&>(*this);
  const auto& b = static_cast<const decimal_impl&>(rhs);

  // Fast path: both finite (handles division by zero too, since 0 is finite).
  if (__builtin_expect(!a.is_special() && !b.is_special(), 1)) {
    const uint64_t mb = b.mantissa();
    if (mb == 0) {  // a/0
      return a.mantissa() ? inf(decimal_impl::mul_sign(a, b)) : nan();
    }
    const uint64_t ma = a.mantissa();
    if (ma == 0) return zero(decimal_impl::mul_sign(a, b));  // 0/b

    // Precompute threshold to avoid a 128-bit division per iteration.
    // scaled * 10 <= threshold  ⟺  (scaled*10)/mb ≤ UINT64_MAX.
    const __uint128_t threshold = static_cast<__uint128_t>(mb) * UINT64_MAX;

    // Both mantissas are in [10^18, 10^19), so clzll ∈ [0, 4] and k ∈ {18, 19}.
    // 1233/4096 slightly underestimates log10(2), so k_est ≤ true_k.
    auto k = (int_fast32_t)(((64 + __builtin_clzll(ma) - __builtin_clzll(mb)) * 1233u) >> 12);
    __uint128_t scaled;
    if (k < 19) {
      scaled = static_cast<__uint128_t>(ma) * POW10[k];
      // Adjust upward at most once due to the underestimate.
      if (scaled * 10 <= threshold) { scaled *= 10; ++k; }
    } else {
      scaled = static_cast<__uint128_t>(ma) * POW10[k = 19];
    }

    const uint64_t rm = static_cast<uint64_t>(scaled / mb) + ((scaled % mb) * 2 >= mb);
    const int64_t rexp = a.biased_exponent() - b.biased_exponent() - k;
    return decimal_impl::make(decimal_impl::mul_sign(a, b), rexp, rm);
  }

  // Slow path: at least one inf or NaN.
  if (a.is_special()) {
    if (a.x[1] || b.is_special()) return nan();  // a=NaN, or inf/{NaN,inf}
    return inf(decimal_impl::mul_sign(a, b));  // inf/finite
  }
  // b is special and a isn't.
  if (b.x[1]) return nan();
  return zero(decimal_impl::mul_sign(a, b));  // finite/inf
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
  // Fast path: both finite.
  if (__builtin_expect(!is_special() && !rhs.is_special(), 1)) {
    const uint64_t ma = mantissa(), mb = rhs.mantissa();
    return ma == mb && (x[0] == rhs.x[0] || ma == 0);
  }

  // Slow path.
  return !(is_nan() || rhs.is_nan()) &&  // at least one NaN → never equal
         x[0] == rhs.x[0] && x[1] == rhs.x[1];  // at least one inf
}

bool decimal::operator!=(const decimal& rhs) const {
  // Fast path: both finite.
  if (__builtin_expect(!is_special() && !rhs.is_special(), 1)) {
    const uint64_t ma = mantissa(), mb = rhs.mantissa();
    return ma != mb || (ma != 0 && x[0] != rhs.x[0]);
  }

  // Slow path.
  return is_nan() || rhs.is_nan() ||  // at least one NaN → true
         x[0] != rhs.x[0];    // different inf → true
}

bool decimal::operator<(const decimal& rhs) const {
  // Fast path: both finite.
  if (__builtin_expect(!is_special() && !rhs.is_special(), 1)) {
    const uint64_t sa = (sign)(x[0] & SIGN_BIT);

    // Different signs: a < b iff a is negative, unless both are zero.
    if (sa != (sign)(rhs.x[0] & SIGN_BIT)) return sa && (mantissa() || rhs.mantissa());

    const uint64_t ma = mantissa(), mb = rhs.mantissa();
    // Same sign: handle zeros.
    if (!ma) return !sa && mb;  // 0 < b iff b != 0 and positive sign
    if (!mb) return sa;         // a < 0 iff negative sign

    // Both non-zero, same sign: compare magnitudes, negate for negative.
    const int64_t ea = exponent(), eb = rhs.exponent();
    if (ea != eb) return sa ? ea > eb : ea < eb;
    return (ma != mb) && (sa ? ma > mb : ma < mb);
  }

  // Slow path: at least one inf or NaN.
  if (is_special()) {
    if (x[1] || !(x[0] & SIGN_BIT)) return false;  // a is NaN or +inf
    // a is -inf: true iff b is not NaN and not -inf
    return !rhs.is_special() || (!rhs.x[1] && !(rhs.x[0] & SIGN_BIT));
  }
  // b is special (a is finite): true iff b is +inf
  return !rhs.x[1] && !(rhs.x[0] & SIGN_BIT);
}

bool decimal::operator<=(const decimal& rhs) const {
  // Fast path: both finite.
  if (__builtin_expect(!is_special() && !rhs.is_special(), 1)) {
    const uint64_t sa = x[0] & SIGN_BIT;
    const uint64_t ma = mantissa(), mb = rhs.mantissa();

    // Different signs: a <= b iff a is negative, or both are zero.
    if (sa != (rhs.x[0] & SIGN_BIT)) return sa || !(ma || mb);

    // Same sign: handle zeros.
    if (!ma) return !sa || !mb;  // a is zero: true iff positive sign or b also zero
    if (!mb) return sa;          // b is zero: a <= 0 iff a is negative

    // Both non-zero, same sign: compare magnitudes, negate for negative.
    const int64_t ea = exponent(), eb = rhs.exponent();
    if (ea != eb) return sa ? ea > eb : ea < eb;
    return sa ? ma >= mb : ma <= mb;
  }

  // Slow path: at least one inf or NaN.
  if (is_special()) {
    if (x[1]) return false;          // a is NaN
    if (!(x[0] & SIGN_BIT)) {
      // a is +inf: a <= b iff b is +inf
      return rhs.is_special() && !rhs.x[1] && !(rhs.x[0] & SIGN_BIT);
    }
    // a is -inf: a <= b iff b is not NaN
    return !rhs.is_special() || !rhs.x[1];
  }
  // b is special, a is finite.
  if (rhs.x[1]) return false;        // b is NaN
  // b is inf: a <= b iff b is +inf
  return !(rhs.x[0] & SIGN_BIT);
}

bool decimal::operator>(const decimal& rhs) const { return rhs < *this; }

bool decimal::operator>=(const decimal& rhs) const { return rhs <= *this; }

// ---------------------------------------------------------------------------
// Stream output
// ---------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const decimal& d) {
  char buf[64];
  const char* p = format_decimal(d, buf);
  return os.write(p, buf + 64 - p), os;
}

}  // namespace decimals

// ---------------------------------------------------------------------------
// std::numeric_limits<decimals::decimal>
// ---------------------------------------------------------------------------

decimals::decimal std::numeric_limits<decimals::decimal>::min() noexcept {
  return decimals::decimal_impl::make(decimals::POSITIVE,
                                      decimals::MIN_EXPONENT + 18, 1);
}

decimals::decimal std::numeric_limits<decimals::decimal>::max() noexcept {
  return decimals::decimal_impl::make(decimals::POSITIVE, decimals::MAX_EXPONENT,
                                      9999999999999999999ULL);
}

decimals::decimal std::numeric_limits<decimals::decimal>::lowest() noexcept {
  return decimals::decimal_impl::make(decimals::NEGATIVE, decimals::MAX_EXPONENT,
                                      9999999999999999999ULL);
}

decimals::decimal std::numeric_limits<decimals::decimal>::epsilon() noexcept {
  return decimals::decimal_impl::make(decimals::POSITIVE, -18, 1);
}

decimals::decimal
std::numeric_limits<decimals::decimal>::round_error() noexcept {
  return decimals::decimal_impl::make(decimals::POSITIVE, -1, 5);  // 0.5
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
