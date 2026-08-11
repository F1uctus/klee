/*===-- cortex-m-smoke.c --------------------------------------------------===//
 *
 *                     The KLEE Symbolic Virtual Machine
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * End-to-end fixture for the Windows CI: a function over symbolic inputs with
 * enough branching to force several distinct paths.
 *
 * It is deliberately freestanding -- no libc headers, no external calls -- so
 * that it compiles for a bare-metal Cortex-M triple and runs under
 * --external-calls=none, which cross-architecture analysis requires because an
 * x86-64 host cannot JIT-call a function using the ARM ABI.
 *
 * The same source is compiled for the native host as well, so one fixture
 * exercises both the "64" and the "arm32" runtime bitcode variants.
 *
 * Compile with -I<klee>/include so that klee/klee.h is found.
 */

#include "klee/klee.h"

typedef unsigned int uint32_t;
typedef int int32_t;

enum {
  LEVEL_UNDERRANGE = 0,
  LEVEL_LOW = 1,
  LEVEL_NOMINAL = 2,
  LEVEL_HIGH = 3,
  LEVEL_FAULT = 4
};

/* Convert a 12bit ADC reading to millivolts against a 3300 mV reference, apply
 * a signed calibration offset, and classify the result. Every branch is
 * reachable, so a complete exploration yields one test per classification plus
 * the out-of-range cases. */
int32_t classify_reading(uint32_t raw, int32_t offset) {
  if (raw > 4095u)
    return LEVEL_FAULT;

  /* 3300 * 4095 fits in 32 bits, so this cannot overflow for a valid raw. */
  int32_t millivolts = (int32_t)((raw * 3300u) / 4095u);

  millivolts += offset;

  if (millivolts < 0)
    return LEVEL_FAULT;
  if (millivolts > 3300)
    return LEVEL_FAULT;

  if (millivolts < 500)
    return LEVEL_UNDERRANGE;
  if (millivolts < 1200)
    return LEVEL_LOW;
  if (millivolts < 2800)
    return LEVEL_NOMINAL;
  return LEVEL_HIGH;
}

/* KLEE starts an --entry-point the way it starts main, passing argc and argv,
 * so the entry point takes no arguments and marks its own locals symbolic
 * instead. Each object is named, which is what makes the generated .ktest
 * readable with ktest-tool. */
int smoke_entry(void) {
  uint32_t raw;
  int32_t offset;

  klee_make_symbolic(&raw, sizeof(raw), "raw");
  klee_make_symbolic(&offset, sizeof(offset), "offset");

  /* Keep the offset in a plausible calibration band. Without this the
   * saturation branches dominate and the interesting classifications become
   * hard to reach within the instruction budget. */
  klee_assume(offset > -2000);
  klee_assume(offset < 2000);

  return classify_reading(raw, offset);
}
