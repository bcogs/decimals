# floating point numbers preserving decimal string representation

The decimals C++ library provides a `decimal` class usable like a `double`, and, when reading a string of decimal digits, can be converted back to a string with the exact same digits.

Conversions:
- Conversion from `double` to `decimal` is exact, but the other way rounds to the nearest double.
- Conversion from `int64_t` to `decimal` is exact, and the other way is exact too if the `decimal` is within the range of numbers representable by `int64_t`.
- Conversion from `uint64_t` to decimal is exact up to `max_exact_uint64` (10^19 - 1) and the other way is exact if the `decimal` is positive and within the range of numbers reprentable by `uint64_t`.

All comparison and arithmetic operations follow IEEE 754 semantics for NaN and infinity (e.g. NaN != NaN, NaN unordered with everything, inf - inf = NaN, 0/0 = NaN, nonzero/0 = +/-inf, etc.).

## Examples

```cpp
#include <iostream>

#include "decimals.h"

using namespace decimals;

int main() {
    // Construction
    decimal a = decimal::from_string("0.1");
    decimal b = decimal::from_string("0.2");
    decimal c(42.0);
    decimal d(int64_t{100});

    // Exact decimal arithmetic — no binary floating-point surprises
    std::cout << (a + b) << "\n";           // 0.3  (not 0.30000000000000004)
    std::cout << a * decimal(int64_t{3}) << "\n";  // 0.3

    // Round-trip through strings preserves digits
    decimal pi = decimal::from_string("3.141592653589793238");
    std::cout << pi << "\n";                // 3.141592653589793238

    // Arithmetic operators work like double
    decimal price = decimal::from_string("19.99");
    decimal tax   = decimal::from_string("0.0725");
    decimal total = price + price * tax;
    std::cout << total << "\n";             // 21.439275

    // Comparisons
    if (total > decimal(int64_t{20}))
        std::cout << "over budget\n";

    // Conversions
    double as_double = total.to_double();
    int64_t truncated = static_cast<int64_t>(total);  // 21
    if (total)
        std::cout << "nonzero\n";

    // Named constructors and queries
    decimal nan = decimal::nan();
    decimal inf = decimal::inf();
    std::cout << nan.is_nan() << " " << inf.is_inf() << "\n";  // 1 1

    // Cheap power-of-10 operations — O(1), no rounding
    decimal km = decimal::from_string("1.5");
    decimal m  = km.mul_pow10(3);          // 1500
    decimal one_million = decimal::pow10(6);

    // Integer log10 — O(1)
    std::cout << decimal::from_string("999").ilog10() << "\n";   // 2
    std::cout << decimal::from_string("1000").ilog10() << "\n";  // 3
}
```
