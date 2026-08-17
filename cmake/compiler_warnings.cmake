#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

###############################################################################
# Compiler warnings
###############################################################################
# FIXME: -Wunused-parameter fires a lot so for now suppress it.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  # clang-cl takes the MSVC spelling of these, where -Wall does not mean what it
  # means to the GNU driver: it is /Wall, which is clang's -Weverything. That
  # turns on -Wc++98-compat among much else, and building KLEE against the LLVM
  # headers then emits hundreds of thousands of warnings -- enough to dominate
  # the build's wall-clock and truncate CI logs. /W4 is the closest equivalent
  # to what -Wall -Wextra is meant to select here.
  add_compile_options("/W4" "/wd4100")
else()
  add_compile_options(
    "-Wall"
    "-Wextra"
    "-Wno-unused-parameter"
  )
endif()

###############################################################################
# Warnings as errors
###############################################################################
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if (WARNINGS_AS_ERRORS)
  add_compile_options("-Werror")
  message(STATUS "Treating compiler warnings as errors")
else()
  message(STATUS "Not treating compiler warnings as errors")
endif()
