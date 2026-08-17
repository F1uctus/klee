// Two shapes at the edges of what --mock-strategy=deterministic can say.
//
// A function with no parameters has nothing to tell its calls apart, so every
// call is the same call and must give the same answer.
//
// A varargs function is the opposite: what distinguishes two calls is exactly
// what a synthesised body cannot reach. Judging such calls equal on their named
// arguments alone would rule out behaviour the real function is free to have,
// so they are left as unrelated as naive leaves them.

// RUN: %clang %s -emit-llvm -g -c -o %t1.bc

// Naive relates nothing, so the nullary reads may disagree.
// RUN: rm -rf %t.naive
// RUN: %klee --output-dir=%t.naive --external-calls=none --mock-policy=all --mock-strategy=naive %t1.bc 2>&1 | FileCheck --check-prefix=NAIVE %s
// NAIVE: ASSERTION FAIL: config == config_again

// Deterministic ties the nullary reads together, and still lets the two log
// calls differ -- which is what leaves the branch on them forking in two.
// RUN: rm -rf %t.det
// RUN: %klee --output-dir=%t.det --external-calls=none --mock-policy=all --mock-strategy=deterministic %t1.bc 2>&1 | FileCheck --check-prefix=DET %s
// RUN: not grep "ASSERTION FAIL" %t.det/messages.txt
// DET: completed paths = 2

#include "klee/klee.h"

#include <assert.h>

// Variadic: the argument that differs between the calls below is not one the
// mock body can spill.
int HAL_Log(int level, ...);

// Nullary: nothing to compare, so all calls are one call.
int HAL_GetConfig(void);

int main(void) {
  int level;
  klee_make_symbolic(&level, sizeof(level), "level");

  int logged_one = HAL_Log(level, 1);
  int logged_two = HAL_Log(level, 2);

  int config = HAL_GetConfig();
  int config_again = HAL_GetConfig();
  assert(config == config_again);

  if (logged_one != logged_two)
    return 1;
  return 0;
}
