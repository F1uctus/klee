// The Windows C runtime spells assert()'s helper _wassert and hands it wide
// strings. Test it directly rather than through assert.h so the test runs on
// every host, not only the one whose runtime declares it.
// RUN: %clang %s -g -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t.bc 2>&1 | FileCheck %s

#include "klee/klee.h"

void _wassert(const unsigned short *message, const unsigned short *file,
              unsigned line);

// "x != 5" as the runtime would pass it.
static const unsigned short message[] = {'x', ' ', '!', '=', ' ', '5', 0};
static const unsigned short file[] = {'a', '.', 'c', 0};

int main(void) {
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");
  if (x == 5)
    _wassert(message, file, 12);
  return 0;
}

// CHECK: ASSERTION FAIL: x != 5
// CHECK: KLEE: done: completed paths = 1
