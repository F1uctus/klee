// A mocked function stands in for one that was never linked in, so nothing
// stops it from answering the same question two different ways. That is what
// --mock-strategy=deterministic forbids: a call whose arguments equal an
// earlier call's is constrained to return what the earlier one returned.

// RUN: %clang %s -emit-llvm -g -c -o %t1.bc

// Naive lets the two reads of the same channel disagree.
// RUN: rm -rf %t.naive
// RUN: %klee --output-dir=%t.naive --external-calls=none --mock-policy=all --mock-strategy=naive %t1.bc 2>&1 | FileCheck --check-prefix=NAIVE %s
// NAIVE: ASSERTION FAIL: first == again

// Deterministic does not, and the path that would have failed is gone.
// RUN: rm -rf %t.det
// RUN: %klee --output-dir=%t.det --external-calls=none --mock-policy=all --mock-strategy=deterministic %t1.bc 2>&1 | FileCheck --check-prefix=DET %s
// RUN: not grep "ASSERTION FAIL" %t.det/messages.txt
// DET: completed paths = 2

// The same holds when the mock is produced on demand rather than synthesised
// into the module up front: both go through the one call history.
// RUN: rm -rf %t.failed
// RUN: %klee --output-dir=%t.failed --external-calls=none --mock-policy=failed --mock-strategy=deterministic %t1.bc 2>&1 | FileCheck --check-prefix=FAILED %s
// RUN: not grep "ASSERTION FAIL" %t.failed/messages.txt
// FAILED: completed paths = 2

// Each call is still its own object in the test case, so replay feeds them back
// one call at a time: two inputs and the three reads.
// RUN: %ktest-tool %t.det/test000001.ktest | FileCheck --check-prefix=KTEST %s
// KTEST: num objects: 5

#include "klee/klee.h"

#include <assert.h>

// Declared, called, never defined: this is what gets mocked.
unsigned hardware_read(unsigned channel);

int main(void) {
  unsigned channel, other;
  klee_make_symbolic(&channel, sizeof(channel), "channel");
  klee_make_symbolic(&other, sizeof(other), "other");
  klee_assume(channel != other);

  unsigned first = hardware_read(channel);
  unsigned again = hardware_read(channel);
  unsigned elsewhere = hardware_read(other);

  assert(first == again);

  // A different channel is under no such obligation, so this still forks --
  // which is what leaves two completed paths rather than one.
  if (elsewhere != first)
    return 1;
  return 0;
}
