// A module whose pointers are narrower than the host's used to abort here.
//
// KLEE registers a handful of objects at the address they have in its own
// process -- errno and the ctype tables -- but MemoryObject builds addresses at
// the module's pointer width. For a 32bit module on a 64bit host those host
// addresses do not survive the truncation, so the first resolution that walked
// over one asserted with "invalid constant" instead of reporting anything.
//
// Nothing in the suite covered this: the only other 32bit test never hands its
// bitcode to klee at all.
//
// REQUIRES: target-arm
// RUN: %clang %s --target=thumbv6m-none-eabi -ffreestanding -emit-llvm -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --libc=klee --external-calls=none \
// RUN:     --mock-policy=failed --symbolic-entry-args --entry-point=classify \
// RUN:     %t1.bc 2>&1 | FileCheck %s

// The relocation is reported rather than done silently, because it does mean
// the object no longer shares an address with the host's copy.
// CHECK: does not fit in a 32-bit pointer

// And execution then proceeds normally instead of aborting.
// CHECK: completed paths = 3

typedef unsigned int uint32_t;

int classify(uint32_t value) {
  if (value > 3000)
    return 2;
  if (value > 1000)
    return 1;
  return 0;
}
