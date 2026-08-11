// A bare number stays GiB, which is how these options have always been spelled.
// RUN: %clang %s -emit-llvm -g -c -o %t1.bc
// RUN: rm -rf %t.gib
// RUN: %klee --output-dir=%t.gib --kdalloc-heap-size=2 %t1.bc 2>&1 | FileCheck --check-prefix=GIB %s
// GIB: heap (start-address={{.*}} size=2048 MiB)

// A suffix selects the unit, so a segment smaller than a gibibyte can be asked
// for. Without this a 32bit module cannot be laid out at all: it has under
// 2 GiB to work with and there are four segments.
// RUN: rm -rf %t.mib
// RUN: %klee --output-dir=%t.mib --kdalloc-heap-size=512M %t1.bc 2>&1 | FileCheck --check-prefix=MIB %s
// MIB: heap (start-address={{.*}} size=512 MiB)

// RUN: rm -rf %t.gsuffix
// RUN: %klee --output-dir=%t.gsuffix --kdalloc-heap-size=3G %t1.bc 2>&1 | FileCheck --check-prefix=GSUFFIX %s
// GSUFFIX: heap (start-address={{.*}} size=3072 MiB)

// Nonsense is rejected rather than silently treated as zero.
// RUN: rm -rf %t.bad
// RUN: not %klee --output-dir=%t.bad --kdalloc-heap-size=lots %t1.bc 2>&1 | FileCheck --check-prefix=BAD %s
// BAD: is not a size

// RUN: rm -rf %t.zero
// RUN: not %klee --output-dir=%t.zero --kdalloc-heap-size=0 %t1.bc 2>&1 | FileCheck --check-prefix=ZERO %s
// ZERO: is not a size

int main(void) { return 0; }
