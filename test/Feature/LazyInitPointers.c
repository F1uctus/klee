// A function handed a structure it did not build has to be given one. The
// object a pointer parameter points at is only the first level; a pointer
// stored inside it points nowhere until something puts an object there.

// RUN: %clang %s -emit-llvm -g -c -o %t1.bc

// With no lazy initialisation the inner pointer is an arbitrary integer, so
// following it is a memory error -- reported as a fault in code that never had
// a chance to be wrong, and the paths behind it are never explored as paths.
// RUN: rm -rf %t.flat
// RUN: %klee --output-dir=%t.flat --symbolic-entry-args --lazy-init-depth=0 --entry-point=walk %t1.bc 2>&1 | FileCheck --check-prefix=FLAT %s
// FLAT: memory error: out of bound pointer

// One level down is enough for this one, and the error is gone.
// RUN: rm -rf %t.deep
// RUN: %klee --output-dir=%t.deep --symbolic-entry-args --lazy-init-depth=1 --entry-point=walk %t1.bc 2>&1 | FileCheck --check-prefix=DEEP %s
// RUN: not grep "memory error" %t.deep/messages.txt
// DEEP: completed paths = 3

// The object it points at is its own input, named after the field that reaches
// it, so a test case can say what the structure looked like.
// RUN: %ktest-tool %t.deep/test000001.ktest | FileCheck --check-prefix=NAMES %s
// NAMES-DAG: name: 'node'
// NAMES-DAG: name: 'node.next'

// Null is still one of the answers -- the pointer is constrained to be null or
// that object, not pinned to it -- so the branch checking for it is reachable.
// The run that took it has no link to describe and says nothing.
// RUN: %ktest-tool %t.deep/test000001.ktest | FileCheck --check-prefix=NULLED %s
// NULLED-NOT: ptr :

// A test case says which of its bytes hold the address, so a consumer can
// rebuild the structure rather than read the pointer as an integer.
// RUN: %ktest-tool %t.deep/test000002.ktest | FileCheck --check-prefix=LINKS %s
// LINKS: ptr : at +0: object 1 + 0

// Two levels reaches the pointer inside that object in turn.
// RUN: rm -rf %t.deeper
// RUN: %klee --output-dir=%t.deeper --symbolic-entry-args --lazy-init-depth=2 --entry-point=walk2 %t1.bc 2>&1 | FileCheck --check-prefix=DEEPER %s
// RUN: not grep "memory error" %t.deeper/messages.txt
// DEEPER: completed paths = 4

// The other half works from the dereference rather than from the argument, so
// it does not need the type and reaches pointers made symbolic any other way --
// which is what a generated harness does, with one klee_make_symbolic over a
// whole structure. It is off by default: giving an object to a pointer that
// resolved to nothing is right when the pointer is an input and wrong when the
// run is looking for faults.
// RUN: rm -rf %t.off
// RUN: %klee --output-dir=%t.off --lazy-init-depth=0 --entry-point=harness %t1.bc 2>&1 | FileCheck --check-prefix=OFF %s
// OFF: memory error

// RUN: rm -rf %t.on
// RUN: %klee --output-dir=%t.on --lazy-init-depth=0 --lazy-init-on-deref --entry-point=harness %t1.bc 2>&1 | FileCheck --check-prefix=ON %s
// RUN: not grep "memory error" %t.on/messages.txt
// ON: completed paths = 3

// The object it invented is named the way a consumer knows to expect, and the
// link back to the byte that addresses it is recorded just the same.
// RUN: %ktest-tool %t.on/test000002.ktest | FileCheck --check-prefix=INVENTED %s
// INVENTED-DAG: name: 'unnamed'
// INVENTED-DAG: ptr : at +0: object 1 + 0

#include "klee/klee.h"

struct node {
  struct node *next;
  int value;
};

int walk(struct node *node) {
  if (!node->next)
    return -1;
  if (node->next->value > 10)
    return 1;
  return 0;
}

int walk2(struct node *node) {
  if (!node->next)
    return -1;
  if (!node->next->next)
    return -2;
  if (node->next->next->value > 10)
    return 1;
  return 0;
}

// The shape a generated harness has: one object made symbolic as a whole, so
// the pointer inside it is symbolic without anything having said it is one.
int harness(void) {
  struct node n;
  klee_make_symbolic(&n, sizeof(n), "n");
  return walk(&n);
}

int main(void) { return 0; }
