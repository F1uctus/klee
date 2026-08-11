//===-- PlatformCompat.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Support/PlatformCompat.h"

#include <atomic>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace klee {

std::size_t getPageSize() {
#if defined(_WIN32)
  // dwPageSize is the granularity of protection and commitment, which is what
  // callers mean by "page size". Note this is *not* dwAllocationGranularity,
  // the coarser (usually 64 KiB) granularity VirtualAlloc rounds base addresses
  // to.
  SYSTEM_INFO info;
  ::GetSystemInfo(&info);
  return static_cast<std::size_t>(info.dwPageSize);
#else
  return static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
#endif
}

bool getProcessUserTime(std::uint64_t &microseconds) {
#if defined(_WIN32)
  FILETIME creation, exit, kernel, user;
  if (!::GetProcessTimes(::GetCurrentProcess(), &creation, &exit, &kernel,
                         &user))
    return false;

  // FILETIME counts 100-nanosecond intervals in two 32-bit halves.
  ULARGE_INTEGER value;
  value.LowPart = user.dwLowDateTime;
  value.HighPart = user.dwHighDateTime;
  microseconds = static_cast<std::uint64_t>(value.QuadPart) / 10u;
  return true;
#else
  rusage usage{};
  if (::getrusage(RUSAGE_SELF, &usage))
    return false;

  microseconds =
      static_cast<std::uint64_t>(usage.ru_utime.tv_sec) * 1000000u +
      static_cast<std::uint64_t>(usage.ru_utime.tv_usec);
  return true;
#endif
}

#if defined(_WIN32)
namespace {

/// Reservations eligible for demand-commit.
///
/// The table is a fixed-size array of atomics rather than a container behind a
/// lock because it is read from a vectored exception handler. That runs in
/// exception context on an arbitrary thread, so it must not allocate and must
/// not be able to block on a lock some other thread is holding. KLEE creates
/// four arenas, so a handful of slots is plenty.
struct LazyRegion {
  std::atomic<std::uintptr_t> base;
  std::atomic<std::size_t> size;
};

constexpr std::size_t maxLazyRegions = 16;
LazyRegion lazyRegions[maxLazyRegions];

std::atomic<bool> lazyHandlerInstalled{false};
std::once_flag lazyHandlerOnce;

/// Commit in blocks rather than single pages: a sequential write over a fresh
/// arena would otherwise take one fault per 4 KiB. 64 KiB matches the Windows
/// allocation granularity.
constexpr std::size_t lazyCommitBlock = 64 * 1024;

LONG CALLBACK lazyCommitHandler(EXCEPTION_POINTERS *info) {
  const EXCEPTION_RECORD *record = info->ExceptionRecord;
  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      record->NumberParameters < 2)
    return EXCEPTION_CONTINUE_SEARCH;

  const auto fault =
      static_cast<std::uintptr_t>(record->ExceptionInformation[1]);

  for (auto &region : lazyRegions) {
    const auto base = region.base.load(std::memory_order_acquire);
    if (base == 0)
      continue;
    const auto size = region.size.load(std::memory_order_acquire);
    if (fault < base || fault - base >= size)
      continue;

    // Round down to a block boundary and clamp to the end of the region.
    const auto offset = ((fault - base) / lazyCommitBlock) * lazyCommitBlock;
    const std::size_t length =
        (size - offset) < lazyCommitBlock ? (size - offset) : lazyCommitBlock;

    // Committing an already-committed page succeeds, so a race between two
    // threads faulting on the same block is harmless.
    if (::VirtualAlloc(reinterpret_cast<LPVOID>(base + offset), length,
                       MEM_COMMIT, PAGE_READWRITE))
      return EXCEPTION_CONTINUE_EXECUTION;

    // Out of commit for real: let the normal crash path report it rather than
    // spinning on the same fault forever.
    return EXCEPTION_CONTINUE_SEARCH;
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

bool registerLazyRegion(std::uintptr_t base, std::size_t size) {
  std::call_once(lazyHandlerOnce, [] {
    // First in the chain, so it sees the fault before anything else.
    lazyHandlerInstalled.store(
        ::AddVectoredExceptionHandler(1, lazyCommitHandler) != nullptr,
        std::memory_order_release);
  });

  if (!lazyHandlerInstalled.load(std::memory_order_acquire))
    return false;

  for (auto &region : lazyRegions) {
    std::uintptr_t expected = 0;
    if (region.base.compare_exchange_strong(expected, base,
                                            std::memory_order_acq_rel)) {
      region.size.store(size, std::memory_order_release);
      return true;
    }
  }
  return false;
}

void unregisterLazyRegion(std::uintptr_t base) {
  for (auto &region : lazyRegions) {
    if (region.base.load(std::memory_order_acquire) == base) {
      region.size.store(0, std::memory_order_release);
      region.base.store(0, std::memory_order_release);
      return;
    }
  }
}

} // namespace

void *reserveLazyCommit(std::uintptr_t preferredAddress, std::size_t size) {
  void *base = ::VirtualAlloc(reinterpret_cast<LPVOID>(preferredAddress), size,
                              MEM_RESERVE, PAGE_READWRITE);
  if (!base)
    return nullptr;

  // A fixed address request either lands exactly or fails; VirtualAlloc does
  // not silently relocate. Check anyway so the caller's contract holds.
  if (preferredAddress != 0 &&
      reinterpret_cast<std::uintptr_t>(base) != preferredAddress) {
    ::VirtualFree(base, 0, MEM_RELEASE);
    return nullptr;
  }

  if (!registerLazyRegion(reinterpret_cast<std::uintptr_t>(base), size)) {
    ::VirtualFree(base, 0, MEM_RELEASE);
    return nullptr;
  }

  return base;
}

bool decommitLazy(void *base, std::size_t size) {
  // The reservation stays; the pages fault back in through the handler.
  return ::VirtualFree(base, size, MEM_DECOMMIT) != 0;
}

bool releaseLazyCommit(void *base, std::size_t size) {
  (void)size; // MEM_RELEASE frees the whole reservation and requires size 0.
  unregisterLazyRegion(reinterpret_cast<std::uintptr_t>(base));
  return ::VirtualFree(base, 0, MEM_RELEASE) != 0;
}
#endif

int createDirectorySymlink(const char *target, const char *linkPath) {
#if defined(_WIN32)
  // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE lets this succeed without
  // elevation when Developer Mode is on. Older Windows builds reject the flag
  // outright, so retry without it before giving up.
  DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY |
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  if (::CreateSymbolicLinkA(linkPath, target, flags))
    return 0;

  if (::GetLastError() == ERROR_INVALID_PARAMETER &&
      ::CreateSymbolicLinkA(linkPath, target, SYMBOLIC_LINK_FLAG_DIRECTORY))
    return 0;

  return -1;
#else
  return ::symlink(target, linkPath);
#endif
}

bool getMinorPageFaultCount(std::uint64_t &faults) {
#if defined(_WIN32)
  // Windows does not distinguish minor from major faults in this API; PageFaultCount
  // is the total. That is an over-estimate, but it is monotonic and the caller only
  // uses differences of it to size a working-set estimate.
  PROCESS_MEMORY_COUNTERS counters;
  counters.cb = sizeof(counters);
  if (!::GetProcessMemoryInfo(::GetCurrentProcess(), &counters,
                              sizeof(counters)))
    return false;

  faults = static_cast<std::uint64_t>(counters.PageFaultCount);
  return true;
#else
  rusage usage{};
  if (::getrusage(RUSAGE_SELF, &usage))
    return false;

  faults = static_cast<std::uint64_t>(usage.ru_minflt);
  return true;
#endif
}

} // namespace klee
