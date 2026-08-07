#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_subdirectory(ktest-gen)
add_subdirectory(kleaver)
add_subdirectory(klee)
add_subdirectory(klee-exec-tree)
add_subdirectory(klee-stats)
add_subdirectory(ktest-tool)

if (NOT WIN32)
  # klee-replay re-runs a test case against the native binary through the
  # POSIX runtime, and ktest-randgen builds its scratch files with mkstemp and
  # fchmod. Both are POSIX by construction rather than incidentally, so they are
  # not part of the Windows build.
  add_subdirectory(ktest-randgen)
  add_subdirectory(klee-replay)
  add_subdirectory(klee-zesti)
endif()
