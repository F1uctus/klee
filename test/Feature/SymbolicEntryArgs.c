// Without --symbolic-entry-args, KLEE starts an entry point the way it starts
// main: the first parameter arrives as argc and the second as the argv pointer.
// Nothing branches, and reading through the bogus second argument ends the run.
// RUN: %clang %s -emit-llvm -g -c -o %t1.bc
// RUN: rm -rf %t.concrete
// RUN: %klee --output-dir=%t.concrete --entry-point=classify %t1.bc 2>&1 | FileCheck --check-prefix=CONCRETE %s
// CONCRETE: completed paths = 0

// With it, each parameter is made symbolic and every branch is reachable.
// RUN: rm -rf %t.symbolic
// RUN: %klee --output-dir=%t.symbolic --symbolic-entry-args --entry-point=classify %t1.bc 2>&1 | FileCheck --check-prefix=SYMBOLIC %s
// SYMBOLIC: completed paths = 3

// The parameters are recorded under their source names, so the test case says
// which argument each value belongs to.
// RUN: %ktest-tool %t.symbolic/test000001.ktest | FileCheck --check-prefix=NAMES %s
// NAMES-DAG: name: 'value'
// NAMES-DAG: name: 'limit'

// A pointer parameter gets an object the size of what it points at, taken from
// the debug information, since an opaque pointer no longer records that itself.
// RUN: rm -rf %t.pointer
// RUN: %klee --output-dir=%t.pointer --symbolic-entry-args --entry-point=inspect %t1.bc 2>&1 | FileCheck --check-prefix=POINTER %s
// POINTER: completed paths = 2

// Built without debug information there is nothing to take the size from, so it
// falls back to a fixed allocation and says so rather than guessing silently.
// RUN: %clang %s -emit-llvm -c -o %t2.bc
// RUN: rm -rf %t.nodebug
// RUN: %klee --output-dir=%t.nodebug --symbolic-entry-args --entry-point=inspect %t2.bc 2>&1 | FileCheck --check-prefix=NODEBUG %s
// NODEBUG: No debug information for the type

struct reading {
  unsigned raw;
  int offset;
};

int classify(unsigned value, unsigned limit) {
  if (value > limit)
    return 2;
  if (value == limit)
    return 1;
  return 0;
}

int inspect(struct reading *r) {
  if (r->raw > 4095)
    return -1;
  return (int)r->raw + r->offset;
}

int main(void) { return 0; }
