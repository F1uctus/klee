// Covering a translation unit meant one process per function, each parsing,
// linking and preparing the whole module again before executing anything. An
// --entrypoints-file runs them in turn against the module prepared once.

// RUN: %clang %s -emit-llvm -g -c -o %t1.bc

// RUN: rm -rf %t.batch %t.list
// RUN: echo "first" > %t.list
// RUN: echo "second" >> %t.list
// RUN: echo "third" >> %t.list
// RUN: %klee --output-dir=%t.batch --symbolic-entry-args --entrypoints-file=%t.list %t1.bc 2>&1 | FileCheck --check-prefix=BATCH %s
// BATCH: Running entry point first
// BATCH: Running entry point second
// BATCH: Running entry point third

// Each writes into its own directory, numbered from one, so a reader can tell
// whose test case is whose without parsing anything.
// RUN: %ktest-tool %t.batch/first/test000001.ktest | FileCheck --check-prefix=FIRST %s
// FIRST: num objects: 1
// RUN: %ktest-tool %t.batch/third/test000002.ktest | FileCheck --check-prefix=THIRD %s
// THIRD: num objects: 1

// A function only reachable as a later entry point is still there when its
// turn comes: preparation drops what the first entry point cannot reach, so
// the batch has to be named before the module is prepared, not after.
// RUN: not grep "not found in module" %t.batch/warnings.txt

// --timeout-per-function bounds one entry point and goes on to the next, where
// --max-time is the budget for the whole run. A function that uses up its own
// time still writes out what it had.
// RUN: rm -rf %t.timeout %t.slowlist
// RUN: echo "spins" > %t.slowlist
// RUN: echo "third" >> %t.slowlist
// RUN: %klee --output-dir=%t.timeout --symbolic-entry-args --entrypoints-file=%t.slowlist --timeout-per-function=2s %t1.bc 2>&1 | FileCheck --check-prefix=TIMEOUT %s
// TIMEOUT: Running entry point spins
// TIMEOUT: Entry point timeout invoked
// TIMEOUT: Running entry point third

int first(int v) {
  if (v > 10)
    return 1;
  return 0;
}

int second(int v) {
  if (v > 20)
    return 2;
  return 0;
}

int third(int v) {
  if (v > 30)
    return 3;
  return 0;
}

// Long enough that a two second budget expires inside it.
int spins(int v) {
  int total = 0;
  for (int i = 0; i < 100000000; i++)
    total += (v * i) % 7;
  return total;
}

int main(void) { return 0; }
