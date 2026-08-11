//===-- mapping.h -----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KDALLOC_MAPPING_H
#define KDALLOC_MAPPING_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <linux/version.h>
#endif

#if defined(__APPLE__)
#include <mach/vm_map.h>
#include <sys/types.h>
#endif

#include "klee/Support/ErrorHandling.h"
#include "klee/Support/PlatformCompat.h"

namespace klee::kdalloc {
/// Sentinel for "this object does not hold a mapping". On POSIX this is mmap's
/// own MAP_FAILED so that behaviour is bit-for-bit unchanged; on Windows, where
/// VirtualAlloc reports failure with NULL, it is nullptr.
#if defined(_WIN32)
#define KDALLOC_MAPPING_INVALID (nullptr)
#else
#define KDALLOC_MAPPING_INVALID (MAP_FAILED)
#endif

class Mapping {
  void *baseAddress = KDALLOC_MAPPING_INVALID;
  std::size_t size = 0;

#if defined(_WIN32)
  bool try_map(std::uintptr_t baseAddress) noexcept {
    assert(this->baseAddress == KDALLOC_MAPPING_INVALID);

    // Reserve address space only and let pages be committed as they are first
    // touched. Committing up front would charge the whole arena against RAM
    // plus pagefile and fail with ERROR_COMMITMENT_LIMIT long before KLEE's
    // default sizes are reached, whereas the POSIX path pays nothing for
    // untouched pages thanks to MAP_NORESERVE.
    void *mappedAddress = klee::reserveLazyCommit(baseAddress, size);
    if (mappedAddress == nullptr) {
      this->baseAddress = KDALLOC_MAPPING_INVALID;
      return false;
    }
    this->baseAddress = mappedAddress;

    return true;
  }
#else
  bool try_map(std::uintptr_t baseAddress) noexcept {
    assert(this->baseAddress == KDALLOC_MAPPING_INVALID);

    int flags = MAP_ANON | MAP_PRIVATE;
#if defined(__linux__)
    flags |= MAP_NORESERVE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
    if (baseAddress != 0) {
      flags |= MAP_FIXED_NOREPLACE;
    }
#endif
#elif defined(__FreeBSD__)
    if (baseAddress != 0) {
      flags |= MAP_FIXED | MAP_EXCL;
    }
#endif

    auto mappedAddress = ::mmap(reinterpret_cast<void *>(baseAddress), size,
                                PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mappedAddress == MAP_FAILED) {
      this->baseAddress = MAP_FAILED;
      return false;
    }
    if (baseAddress != 0 &&
        baseAddress != reinterpret_cast<std::uintptr_t>(mappedAddress)) {
      [[maybe_unused]] int rc = ::munmap(mappedAddress, size);
      assert(rc == 0 && "munmap failed");
      this->baseAddress = MAP_FAILED;
      return false;
    }
    this->baseAddress = mappedAddress;

#if defined(__linux__)
    {
      [[maybe_unused]] int rc =
          ::madvise(this->baseAddress, size,
                    MADV_NOHUGEPAGE | MADV_DONTFORK | MADV_RANDOM);
      assert(rc == 0 && "madvise failed");
    }
#elif defined(__FreeBSD__)
    {
      [[maybe_unused]] int rc =
          ::minherit(this->baseAddress, size, INHERIT_NONE);
      assert(rc == 0 && "minherit failed");
    }
#elif defined(__APPLE__)
    {
      [[maybe_unused]] int rc =
          ::minherit(this->baseAddress, size, VM_INHERIT_NONE);
      assert(rc == 0 && "minherit failed");
    }
#endif

    return true;
  }
#endif

public:
  Mapping() = default;

  explicit Mapping(std::size_t size) noexcept : Mapping(0, size) {}

  Mapping(std::uintptr_t baseAddress, std::size_t size) noexcept : size(size) {
    try_map(baseAddress);
  }

  Mapping(Mapping const &) = delete;
  Mapping &operator=(Mapping const &) = delete;

  Mapping(Mapping &&other) noexcept
      : baseAddress(other.baseAddress), size(other.size) {
    other.baseAddress = KDALLOC_MAPPING_INVALID;
    other.size = 0;
  }
  Mapping &operator=(Mapping &&other) noexcept {
    if (&other != this) {
      using std::swap;
      swap(other.baseAddress, baseAddress);
      swap(other.size, size);
    }
    return *this;
  }

  [[nodiscard]] void *getBaseAddress() const noexcept {
    assert(*this && "Invalid mapping");
    return baseAddress;
  }

  [[nodiscard]] std::size_t getSize() const noexcept { return size; }

  void clear() {
    assert(*this && "Invalid mapping");

#if defined(_WIN32)
    // Dropping the physical backing keeps the reservation, so the arena stays
    // at the address KDAlloc handed out and the pages simply fault back in on
    // next use. This is the equivalent of Linux's MADV_DONTNEED, and like it
    // avoids the teardown and re-reserve the generic POSIX path needs.
    [[maybe_unused]] bool ok = klee::decommitLazy(baseAddress, size);
    assert(ok && "could not decommit the mapping");
#elif defined(__linux__)
    [[maybe_unused]] int rc = ::madvise(baseAddress, size, MADV_DONTNEED);
    assert(rc == 0 && "madvise failed");
#else
    auto address = reinterpret_cast<std::uintptr_t>(baseAddress);
    [[maybe_unused]] int rc = ::munmap(baseAddress, size);
    assert(rc == 0 && "munmap failed");
    baseAddress = MAP_FAILED;
    [[maybe_unused]] auto success = try_map(address);
    assert(success && "could not recreate the mapping");
#endif
  }

  explicit operator bool() const noexcept {
    return baseAddress != KDALLOC_MAPPING_INVALID;
  }

  ~Mapping() {
    if (*this) {
#if defined(_WIN32)
      [[maybe_unused]] bool ok = klee::releaseLazyCommit(baseAddress, size);
      assert(ok && "could not release the mapping");
#else
      [[maybe_unused]] int rc = ::munmap(baseAddress, size);
      assert(rc == 0 && "munmap failed");
#endif
    }
  }
};
} // namespace klee::kdalloc

#endif
