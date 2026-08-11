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

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <malloc.h>
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

/// Allocates \p size bytes aligned to at least \p alignment, or returns
/// nullptr. Memory obtained here must be released with freeAligned(), which
/// matters on Windows where the aligned allocator has its own deallocator.
inline void *allocateAligned(std::size_t size, std::size_t alignment) {
#if defined(_WIN32)
  // _aligned_malloc is used for every alignment, not just the large ones: its
  // result must always go back through _aligned_free, so mixing it with plain
  // malloc for small alignments would make the two paths impossible to tell
  // apart at deallocation time.
  return ::_aligned_malloc(size, alignment < 8 ? 8 : alignment);
#else
  if (alignment <= 8)
    return ::malloc(size);

  void *result = nullptr;
  if (::posix_memalign(&result, alignment, size))
    return nullptr;
  return result;
#endif
}

/// Releases memory obtained from allocateAligned().
inline void freeAligned(void *ptr) {
#if defined(_WIN32)
  ::_aligned_free(ptr);
#else
  ::free(ptr);
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

/// Seeds the pseudo-random generator behind randomNumber().
inline void seedRandom(unsigned seed) {
#if defined(_WIN32)
  ::srand(seed);
#else
  ::srandom(seed);
#endif
}

/// Returns a non-negative pseudo-random number. KLEE only uses this to make
/// arbitrary-but-reproducible choices, so the weaker guarantees of rand() on
/// Windows are acceptable; what matters is that it is deterministic after
/// seedRandom().
inline long randomNumber() {
#if defined(_WIN32)
  return ::rand();
#else
  return ::random();
#endif
}

/// Creates a directory. \p mode carries the POSIX permission bits and is
/// ignored on Windows, which has no equivalent. Returns 0 on success, -1 on
/// failure with errno set (EEXIST when the directory already exists).
inline int makeDirectory(const char *path, unsigned mode) {
#if defined(_WIN32)
  (void)mode;
  return ::_mkdir(path);
#else
  return ::mkdir(path, mode);
#endif
}

/// Removes a symbolic link previously created by createDirectorySymlink().
/// Returns 0 on success, -1 on failure with errno set (ENOENT if absent).
///
/// This is not the same as removeFile(): on Windows a symlink to a directory is
/// itself a directory as far as the API is concerned and has to be removed with
/// RemoveDirectory, while _unlink() rejects it with EACCES.
int removeDirectorySymlink(const char *linkPath);

/// Creates \p linkPath as a symbolic link to the directory \p target. Returns 0
/// on success and -1 on failure.
///
/// This is best-effort: on Windows creating a symlink needs either Developer
/// Mode or elevation, so callers must treat failure as non-fatal.
int createDirectorySymlink(const char *target, const char *linkPath);

#if defined(_WIN32)
/// Reserves \p size bytes of address space, optionally at \p preferredAddress
/// (0 means anywhere), and arranges for pages to be committed the first time
/// they are touched. Returns the base address, or nullptr on failure.
///
/// This is the Windows counterpart of mmap(MAP_NORESERVE). Committing up front
/// is not an option: commit is charged against RAM plus pagefile, so reserving
/// KLEE's default multi-hundred-GiB allocator arenas that way fails with
/// ERROR_COMMITMENT_LIMIT. Reserving address space alone is nearly free, and
/// 64bit processes have address space to spare.
void *reserveLazyCommit(std::uintptr_t preferredAddress, std::size_t size);

/// Drops the physical backing of a lazy reservation while keeping the address
/// range reserved, so the pages fault back in on next use. The equivalent of
/// madvise(MADV_DONTNEED).
bool decommitLazy(void *base, std::size_t size);

/// Releases a reservation obtained from reserveLazyCommit().
bool releaseLazyCommit(void *base, std::size_t size);
#endif

/// Returns the address of the calling thread's errno.
inline int *getErrnoLocation() {
#if defined(_WIN32)
  return ::_errno();
#elif !defined(__APPLE__) && !defined(__FreeBSD__)
  return __errno_location();
#else
  return __error();
#endif
}

} // namespace klee

#endif /* KLEE_PLATFORMCOMPAT_H */
