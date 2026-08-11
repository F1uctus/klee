//===-- KTest.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_KTEST_H
#define KLEE_KTEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  /* Describes a pointer stored inside an object's bytes: at `offset` there is
     a pointer to object `index`, `indexOffset` bytes into it.

     Without this a consumer cannot tell a pointer from an integer that happens
     to hold the same bits, so it cannot reconstruct a linked structure from a
     test case. KLEEF introduced the field and UnitTestBot's renderer relies on
     it. */
  typedef struct Pointer Pointer;
  struct Pointer {
    uint64_t offset;
    uint64_t index;
    uint64_t indexOffset;
  };

  typedef struct KTestObject KTestObject;
  struct KTestObject {
    char *name;
    /* The address the object had during the run. A consumer matches a pointer
       value against it to find which object was pointed at. */
    uint64_t address;
    unsigned numBytes;
    unsigned char *bytes;
    unsigned numPointers;
    Pointer *pointers;
  };
  
  typedef struct KTest KTest;
  struct KTest {
    /* file format version */
    unsigned version; 
    
    unsigned numArgs;
    char **args;

    unsigned symArgvs;
    unsigned symArgvLen;

    unsigned numObjects;
    KTestObject *objects;

    /* How many additional copies of each test the consumer should materialise
       for uninitialised-memory variants. Zero means just the one. */
    unsigned uninitCoeff;
  };

  
  /* returns the current .ktest file format version */
  unsigned kTest_getCurrentVersion();
  
  /* return true iff file at path matches KTest header */
  int   kTest_isKTestFile(const char *path);

  /* returns NULL on (unspecified) error */
  KTest* kTest_fromFile(const char *path);

  /* returns 1 on success, 0 on (unspecified) error */
  int   kTest_toFile(KTest *, const char *path);
  
  /* returns total number of object bytes */
  unsigned kTest_numBytes(KTest *);

  void  kTest_free(KTest *);

#ifdef __cplusplus
}
#endif

#endif /* KLEE_KTEST_H */
