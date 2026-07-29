#pragma once

#include <cstddef>

// ============================================================
// Global configuration for Dynamic Range Sort (DRS) - v7
// ============================================================
//
// DRS_ENABLE_METRICS is the single macro that separates the production
// build from the research build:
//
//   Undefined (production): DRSMetrics, the debug/introspection API
//   (debugPartitionOnly), and every instrumentation call site are
//   compiled out entirely - not disabled at runtime, not present in the
//   compiled object code at all. The production binary contains no
//   histograms, timers, counters, correlations, or logging of any kind.
//
//   Defined (research, e.g. `-DDRS_ENABLE_METRICS`): the full metrics
//   system from v4-v6 is compiled in, unchanged in behavior.
//
// v4-v6 used a runtime constant (DEBUG_METRICS) for this instead; v7
// replaces that with a compile-time macro because runtime-disabled
// metrics code still occupies the binary and can still show up in
// profiles and instruction-cache pressure even when every call is a
// no-op. See ANALYSIS_v7.md, "2. Separar produccion e investigacion",
// for the measured difference between the two builds.
namespace drs {

// lambda: OCUPACION objetivo por bin. Determina cuantos bins se crean
// (initialBins = ceil(n/lambda)) y en cuantas partes se subdivide un bin
// que se refina (splits = ceil(count/lambda)).
//
// v3 lo retuneo de 16 a 64. El paso 8 de v10 lo baja a 32 al separarlo del
// umbral de hoja: con lambda = t la ocupacion es Poisson(lambda) y
// P(X > t) ~ 0,5, es decir la mitad de los elementos entraba en refine()
// por aritmetica y no por los datos. Con lambda=32 y t=64,
// P(Poisson(32) > 64) ~ 1e-8 y refine() se vacia para datos uniformes.
// Medido: refine pasa del 16,8% al 2,4% del tiempo en RandomUniform, y el
// total baja un 16-22% en los cuatro datasets sin redundancia.
// Ver STEP8_lambda_tau.md.
constexpr std::size_t DEFAULT_TARGET_ELEMENTS_PER_BIN = 32;

// t: umbral del CASO BASE. Un bin con <= t elementos deja de refinarse.
// Debe ser >= lambda. El barrido del paso 4b de v9 mostro que la curva de
// tiempo es plana entre 24 y 64 con lambda = t; separandolos, lambda cae al
// lado barato (menos comparaciones de insercion) y t se queda arriba (nada
// se refina).
constexpr std::size_t DEFAULT_LEAF_THRESHOLD = 64;

// Los umbrales del despachador local siguen al UMBRAL DE HOJA, no a lambda.
// Es la observacion O11 de v9: si siguieran a lambda, las hojas de tamano
// entre lambda y t se irian a QuickSort sin que nadie lo hubiera pedido, y
// las comparaciones bajarian mientras el tiempo sube.
constexpr std::size_t INSERTION_SORT_THRESHOLD = DEFAULT_LEAF_THRESHOLD;
constexpr std::size_t QUICKSORT_THRESHOLD = DEFAULT_LEAF_THRESHOLD * 6;

// Maximum number of times a single bin may be recursively refined before
// the remainder is handed to local sorting regardless of its size.
//
// This is NOT a safety net that costs asymptotic quality - it is what
// makes the worst case linear, which is the opposite of what v9 initially
// assumed. Each refinement level consumes log2(splits) bits of the bin's
// observed span, and a span has at most w bits. The branching factor is
// splits = ceil(m/lambda), so a bin can only survive D degenerate levels if
// D * log2(m/lambda) <= w, i.e.
//
//     m  <=  lambda * 2^(w/D)  =  32 * 2^(64/6)  ~=  52016
//
// Note it is LAMBDA that enters this bound, not the leaf threshold: lowering
// lambda from 64 to 32 in the step 8 of v10 HALVED the worst-case residual,
// from ~104032 to ~52016.
//
// The residual handed to Introsort is therefore bounded by a CONSTANT
// independent of n, and its aggregate cost is n*log2(52016) ~= 16n.
// Measured confirmation: the comparison-count exponent of the adversarial
// dataset is 0.994 [0.993, 0.995] over three orders of magnitude of n.
// See documentacion/COMPLEXITY_REVIEW_v9.md and
// documentacion/O8_resolucion_y_reversion_paso3.md.
//
// HARD CONSTRAINT: raising DEFAULT_TARGET_ELEMENTS_PER_BIN (lambda) to 1024
// or beyond makes m_max ~= 1.66e6, no longer small compared to realistic n,
// and the Theta(n log n) term reappears. Any change to lambda must re-check
// this bound. Raising the LEAF THRESHOLD does not affect it.
constexpr std::size_t MAX_SUBDIVISION_DEPTH = 6;

} // namespace drs
