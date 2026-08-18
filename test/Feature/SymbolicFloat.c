// A float reaching a comparison was pinned to one value, and so was everything
// it had been computed from -- the branch was decided before it was reached, so
// only one side of it was ever explored and the test case recorded whatever the
// solver happened to pick.

// RUN: %clang %s -emit-llvm -g -c -o %t1.bc

// Concretising is still the default, and still says so.
// RUN: rm -rf %t.concrete
// RUN: %klee --output-dir=%t.concrete --entry-point=classify_driver %t1.bc 2>&1 | FileCheck --check-prefix=CONCRETE %s
// CONCRETE: silently concretizing (reason: floating point)
// CONCRETE: completed paths = 1

// --fp-runtime hands the comparison to the solver instead, and all three ranges
// come out.
// RUN: rm -rf %t.symbolic
// RUN: %klee --output-dir=%t.symbolic --fp-runtime --entry-point=classify_driver %t1.bc 2>&1 | FileCheck --check-prefix=SYMBOLIC %s
// RUN: not grep "reason: floating point" %t.symbolic/messages.txt
// SYMBOLIC: completed paths = 3

// Arithmetic too, so the solver reasons about the operation and not only the
// input it was given.
// RUN: rm -rf %t.arith
// RUN: %klee --output-dir=%t.arith --fp-runtime --entry-point=scale_driver %t1.bc 2>&1 | FileCheck --check-prefix=ARITH %s
// RUN: not grep "reason: floating point" %t.arith/messages.txt
// ARITH: completed paths = 3

// NaN is a value like any other, and the ordered comparisons are false for it
// while the unordered ones are true -- which is the whole difference between
// the two families, and is a branch of its own.
// An integer that reaches a float conversion was pinned for the rest of the
// path, so every branch downstream of it became infeasible -- the same
// three-branch function yielding one test case, from the other direction.
// RUN: rm -rf %t.conv.concrete
// RUN: %klee --output-dir=%t.conv.concrete --entry-point=convert_driver %t1.bc 2>&1 | FileCheck --check-prefix=CONVCONC %s
// CONVCONC: completed paths = 1

// RUN: rm -rf %t.conv
// RUN: %klee --output-dir=%t.conv --fp-runtime --entry-point=convert_driver %t1.bc 2>&1 | FileCheck --check-prefix=CONV %s
// RUN: not grep "reason: floating point" %t.conv/messages.txt
// CONV: completed paths = 3

// Back the other way, and between the two float formats, and negation.
// RUN: rm -rf %t.round
// RUN: %klee --output-dir=%t.round --fp-runtime --entry-point=roundtrip_driver %t1.bc 2>&1 | FileCheck --check-prefix=ROUND %s
// RUN: not grep "reason: floating point" %t.round/messages.txt
// ROUND: completed paths = 3

// RUN: rm -rf %t.nan
// RUN: %klee --output-dir=%t.nan --fp-runtime --entry-point=ordering_driver %t1.bc 2>&1 | FileCheck --check-prefix=NAN %s
// NAN: completed paths = 2

// x87's 80-bit long double is the one format the solver has no sort for -- its
// significand carries an explicit integer bit, which the interchange formats do
// not -- and it keeps the old behaviour rather than being answered wrongly.
// That is not checked here: long double is 64 bits on some of the targets this
// suite runs on, where there is nothing to fall back from.

#include "klee/klee.h"

int classify(float reading) {
  if (reading > 100.0f)
    return 2;
  if (reading > 10.0f)
    return 1;
  return 0;
}

int classify_driver(void) {
  float reading;
  klee_make_symbolic(&reading, sizeof(reading), "reading");
  return classify(reading);
}

int scale_driver(void) {
  float raw;
  klee_make_symbolic(&raw, sizeof(raw), "raw");
  float volts = raw * 3.3f / 4095.0f;
  if (volts > 3.0f)
    return 2;
  if (volts > 1.5f)
    return 1;
  return 0;
}

// !(a < b) is not a >= b once NaN is possible: the first is true for NaN and
// the second is false, so both branches are reachable.
int ordering_driver(void) {
  double a, b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  if (!(a < b) != (a >= b))
    return 1;
  return 0;
}

// An integer on the way in: without the conversion the multiplication never
// sees anything but one pinned value.
int convert_driver(void) {
  int raw;
  klee_make_symbolic(&raw, sizeof(raw), "raw");
  float volts = (float)raw * 3.3f / 4095.0f;
  if (volts > 3.0f)
    return 2;
  if (volts > 1.0f)
    return 1;
  return 0;
}

// An integer on the way out, a widening and a narrowing between the two
// formats, and a negation -- which is the sign bit and needs no float sort.
int roundtrip_driver(void) {
  int raw;
  klee_make_symbolic(&raw, sizeof(raw), "raw");
  double wide = (double)raw / 4095.0;
  int step = (int)(-(float)wide * -10.0f);
  if (step > 7)
    return 2;
  if (step > 3)
    return 1;
  return 0;
}
