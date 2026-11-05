# Legendre engine: Holmes & Featherstone method

`gravlab::legendre_hf` computes the fully normalized associated Legendre
functions P̄ₙₘ(cos θ) for every 0 ≤ m ≤ n ≤ N at one colatitude θ, up to
N = 2700. It is the first building block for evaluating EGM2008 gravity.

Files: `include/gravlab/legendre_holmes_and_featherstone.hpp`,
`src/legendre_holmes_and_featherstone.cpp`,
`tests/test_legendre_holmes_and_featherstone.cpp`.

## What it computes

- **Normalization:** 4π (geodesy), with no Condon–Shortley phase. This is the
  convention EGM2008 uses, so P̄ₙₘ pairs one-to-one with C̄ₙₘ and S̄ₙₘ.
- **Layout:** values are stored in triangular order,
  `slot_index(n, m) = n(n+1)/2 + m`, the same layout the EGM2008 parser uses
  (`include/gravlab/triangular.hpp`).
- **Input:** the geocentric colatitude θ in radians, 0 ≤ θ ≤ π.

## The method

With t = cos θ and u = sin θ, the standard forward column recursion is:

```
P̄00 = 1,   P̄11 = √3·u
P̄mm = u·√((2m+1)/(2m))·P̄(m−1,m−1)            m ≥ 2     (diagonal)
P̄nm = aₙₘ·t·P̄(n−1,m) − bₙₘ·P̄(n−2,m)          n > m     (down column m)
aₙₘ = √[(2n−1)(2n+1) / ((n−m)(n+m))]
bₙₘ = √[(2n+1)(n+m−1)(n−m−1) / ((n−m)(n+m)(2n−3))]
```

Every value in column m carries a factor uᵐ, and that is what breaks at high
degree. At degree 2190, u²¹⁹⁰ falls below the smallest double once θ is more
than about 45° from the equator. Whole columns then become zero, even though
values further down those columns are of order 1. The test
`UnscaledRecursionUnderflowsWhereScaledOneDoesNot` shows this: at 70° latitude
the plain recursion is 24% wrong in the identity below.

**Holmes & Featherstone (2002)** run the recursion on Xₙₘ = s·P̄ₙₘ/uᵐ, with a
fixed scale s ≈ 10⁻²⁸⁰:

- The recursion is linear within a column, so the same aₙₘ and bₙₘ apply to X.
- The diagonal seeds Xₘₘ = s·√3·∏√((2k+1)/(2k)) don't depend on θ, so they are
  precomputed once, together with aₙₘ and bₙₘ.
- Each value is unscaled as P̄ₙₘ = Xₙₘ·uᵐ/s only when it is stored.

They show that this stays inside the double range for every θ up to degree
2700, which is where `kMaxDegree` comes from.

## Three details that differ from a textbook transcription

1. **The scale is a power of two:** s = 2⁻⁹³⁰ ≈ 1.1·10⁻²⁸⁰. Multiplying by a
   power of two is exact, so the scaling adds no rounding error. The paper's
   10⁻²⁸⁰ doesn't have this property.

2. **The coefficients are rounded the way SHTOOLS does it.** Each integer
   factor gets its own square root, for example
   aₙₘ = √(2n+1)·√(2n−1)/√(n+m)/√(n−m), and factors that need no root stay
   exact (n for m = 0, √(2m+3) one below the diagonal).
   - This matters near the poles. At t = ±1 the column recursion has a
     repeated characteristic root, so a *systematic* rounding bias in the
     coefficients accumulates like n² instead of averaging out.
   - Taking a single square root of the whole ratio gave 6·10⁻¹¹ error at
     degree 2190 at the pole. This formulation gives 4·10⁻¹², the same as
     pyshtools. Perfectly rounded coefficients would give 2·10⁻¹².

3. **The unscaling factor uᵐ/s is kept normal.** Left alone, it turns subnormal
   once uᵐ < ~10⁻⁵⁸⁸. Values as large as 10⁻²⁰⁰ then lose about five digits;
   pyshtools 4.14 shows exactly this. Here, the factor is multiplied back up
   by 2⁹⁰⁰ (exact) whenever it drops below 2⁻⁹⁰⁰. The accumulated power of two
   is applied with one `std::ldexp`, so a value only loses precision if it is
   itself below the double range.

## Using it

```cpp
#include "gravlab/legendre_holmes_and_featherstone.hpp"
namespace hf = gravlab::legendre_hf;

static hf::RecursionStorage<2190> tables;   // ~38 MB: aₙₘ, bₙₘ, seeds
static hf::ValueStorage<2190> values;       // ~19 MB: one set of P̄ₙₘ

hf::Recursion recursion;
if (recursion.init(tables.buffers(), 2190) != hf::Status::ok) { /* ... */ }

// For each colatitude (radians):
if (hf::evaluate(recursion, theta, 2190, values.data(), values.size()) == hf::Status::ok) {
  double p_2190_5 = values.data()[gravlab::slot_index(2190, 5)];
}
```

- `init()` runs once, about 75 ms at degree 2190.
- An initialized `Recursion` is read-only, so several threads can share it,
  each with its own `ValueStorage`.
- `evaluate()` can use any degree up to the one initialized, and writes
  nothing if it fails.

## Validation (all in `tests/test_legendre_holmes_and_featherstone.cpp`)

| Check | Result |
|---|---|
| Closed forms for n ≤ 3, at 22 colatitudes from pole to pole | to 1e-14 |
| 24 values from mpmath's hypergeometric `legenp` at 50 digits, up to degree 2190 (`tools/legendre_reference.py`) | worst relative error 8e-12 |
| 8 tiny values (down to 1e-635) from a 60-digit recursion | in range: 1e-12 or better; below the double range: flushed to 0 |
| Σₘ P̄ₙₘ² = 2n+1 for every n ≤ 2190 and ≤ 2700, at 22 colatitudes | 1.1e-11 away from the poles; 2.6e-10 within 0.1° of a pole (see below) |
| Parity P̄ₙₘ(π−θ) = (−1)ⁿ⁺ᵐ P̄ₙₘ(θ), and the pole values | pass |
| Every value finite, every θ, degree 2700 | pass |
| Full triangle vs pyshtools 4.14 `PlmBar`, degree 2190 (`tools/validate_legendre_hf.py`) | 5e-15 to 7e-14 at mid-latitudes; 1e-12 near the poles |
| No heap allocation in `init()` or `evaluate()` | 0 allocations |

**Near the poles, the limit is the input.** cos θ is stored with an absolute
error of about 10⁻¹⁶. Near t = 1, that is a relative error of up to 10⁻⁸ in
1 − t, and P̄ₙ₀ amplifies it by about n². Against exact values at the same
double θ, P̄₂₁₉₀,₀ at 0.01° is off by 1.3·10⁻¹⁰, here and in pyshtools alike.
For m ≥ 1, this engine is about 30× closer to the exact values than pyshtools,
because pyshtools rebuilds sin θ as √((1−z)(1+z)). Doing better would need a
recursion written in terms of 1 − t = 2 sin²(θ/2), which neither the paper nor
SHTOOLS does.

## Limits

- **Degree ≤ 2700** (`kMaxDegree`), the range for which Holmes & Featherstone
  show the scaled recursion stays in double range.
- **Values only, no derivatives.** dP̄/dθ is the next step for accelerations.
  Holmes & Featherstone's derivative formula has a 1/sin θ, which is what
  fails on polar orbits in Fraerman et al. (2026).
- **Memory access is not optimized.** Column-by-column computation writes into
  the row-ordered layout with a stride of n. That is likely most of the
  ~50 ms per evaluation at degree 2190, and is for the benchmarking task to
  measure.

## References

- S. A. Holmes, W. E. Featherstone (2002), *A unified approach to the Clenshaw
  summation and the recursive computation of very high degree and order
  normalised associated Legendre functions*, J. Geodesy 76, 279–299,
  doi:10.1007/s00190-002-0216-2.
- M. A. Wieczorek, M. Meschede (2018), *SHTools: Tools for working with
  spherical harmonics*, Geochem. Geophys. Geosyst. 19, 2574–2592 (`PlmBar`).
- A. V. Fraerman et al. (2026), Advances in Space Research 78, 5267–5278
  (multi-threaded Holmes; the source of this task).
