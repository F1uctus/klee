#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_library(kleeSupport
  CompressionStream.cpp
  ErrorHandling.cpp
  FileHandling.cpp
  MemoryUsage.cpp
  PlatformCompat.cpp
  PrintVersion.cpp
  RNG.cpp
  Time.cpp
  Timer.cpp
  TreeStream.cpp
)

llvm_config(kleeSupport "${USE_LLVM_SHARED}" support)

target_link_libraries(kleeSupport PRIVATE ${ZLIB_LIBRARIES} ${TCMALLOC_LIBRARIES})

if (WIN32)
  # PlatformCompat.cpp and MemoryUsage.cpp use GetProcessMemoryInfo(). Defining
  # PSAPI_VERSION=2 routes it to kernel32 rather than the legacy psapi.dll.
  target_compile_definitions(kleeSupport PRIVATE PSAPI_VERSION=2)
  target_link_libraries(kleeSupport PRIVATE psapi)
endif()
target_include_directories(kleeSupport PRIVATE ${KLEE_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS} ${TCMALLOC_INCLUDE_DIR})
target_compile_options(kleeSupport PRIVATE ${KLEE_COMPONENT_CXX_FLAGS})
target_compile_definitions(kleeSupport PRIVATE ${KLEE_COMPONENT_CXX_DEFINES})
