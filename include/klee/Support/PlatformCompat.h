//===-- PlatformCompat.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Thin portability layer for the handful of operating system services KLEE
// needs that are spelled differently on POSIX and on Windows. The declarations
// here are platform independent; <windows.h> is confined to PlatformCompat.cpp
// so that it does not leak its macros into the rest of the codebase.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_PLATFORMCOMPAT_H
#define KLEE_PLATFORMCOMPAT_H

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace klee {

/// Returns the size of a virtual memory page in bytes.
std::size_t getPageSize();

/// Stores the time this process has spent in user mode, in microseconds.
/// Returns false (leaving \p microseconds untouched) if the platform reported
/// an error.
bool getProcessUserTime(std::uint64_t &microseconds);

/// Stores the number of minor page faults this process has incurred, i.e.
/// faults that were served without I/O. Returns false (leaving \p faults
/// untouched) if the platform cannot report the value.
///
/// KLEE uses this to estimate how much of its deterministic allocation arena is
/// actually resident; a platform that cannot report it degrades the estimate but
/// stays correct.
bool getMinorPageFaultCount(std::uint64_t &faults);

/// Returns the identifier of the calling process.
inline int getProcessID() {
#if defined(_WIN32)
  return ::_getpid();
#else
  return ::getpid();
#endif
}

/// Deletes \p path. Returns 0 on success, -1 on failure with errno set.
inline int removeFile(const char *path) {
#if defined(_WIN32)
  return ::_unlink(path);
#else
  return ::unlink(path);
#endif
}

/// Changes the working directory to \p path. Returns 0 on success, -1 on
/// failure with errno set.
inline int changeDirectory(const char *path) {
#if defined(_WIN32)
  return ::_chdir(path);
#else
  return ::chdir(path);
#endif
}

/// Returns true if the given file descriptor refers to a terminal.
inline bool isTerminal(int fd) {
#if defined(_WIN32)
  return ::_isatty(fd) != 0;
#else
  return ::isatty(fd) != 0;
#endif
}

} // namespace klee

#endif /* KLEE_PLATFORMCOMPAT_H */
