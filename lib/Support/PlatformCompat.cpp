//===-- PlatformCompat.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Support/PlatformCompat.h"

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
