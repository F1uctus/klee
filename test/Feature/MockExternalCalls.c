// Without mocking, refusing external calls kills the state at the first one.
// RUN: %clang %s -emit-llvm -g -c -o %t1.bc
// RUN: rm -rf %t.none
// RUN: %klee --output-dir=%t.none --external-calls=none %t1.bc 2>&1 | FileCheck --check-prefix=NONE %s
// NONE: completed paths = 0

// --mock-policy=failed answers the call that could not be dispatched.
// RUN: rm -rf %t.failed
// RUN: %klee --output-dir=%t.failed --external-calls=none --mock-policy=failed %t1.bc 2>&1 | FileCheck --check-prefix=FAILED %s
// FAILED: returning a symbolic value instead
// FAILED: completed paths = 3

// --mock-policy=all synthesises the definition up front instead.
// RUN: rm -rf %t.all
// RUN: %klee --output-dir=%t.all --external-calls=none --mock-policy=all %t1.bc 2>&1 | FileCheck --check-prefix=ALL %s
// ALL: Mocking external function hardware_read
// ALL: completed paths = 3

// Either way the mocked value is recorded under the name of the function it
// stood in for, so a test case can be read back against the call it came from.
// RUN: %ktest-tool %t.failed/test000001.ktest | FileCheck --check-prefix=KTEST %s
// KTEST: name: 'hardware_read'

// The deterministic strategy needs uninterpreted functions in the solver, which
// this build does not provide. It must say so rather than silently behave as
// though it were naive.
// RUN: rm -rf %t.det
// RUN: %klee --output-dir=%t.det --external-calls=none --mock-policy=all --mock-strategy=deterministic %t1.bc 2>&1 | FileCheck --check-prefix=DET %s
// DET: not supported by this build

#include "klee/klee.h"

// Declared and called but never defined: this is the boundary KLEE cannot see
// past, and the reason firmware analysis needs mocking at all.
unsigned hardware_read(unsigned channel);

int main(void) {
  unsigned channel;
  klee_make_symbolic(&channel, sizeof(channel), "channel");
  klee_assume(channel < 4);

  unsigned value = hardware_read(channel);

  if (value > 3000)
    return 2;
  if (value > 1000)
    return 1;
  return 0;
}
