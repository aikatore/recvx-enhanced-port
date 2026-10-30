cmake_minimum_required(VERSION 3.20)
project(recvx_pc VERSION 0.7.0 LANGUAGES C CXX)   # the release version (pc/server reads it from here too)
include(cmake/version.cmake)

# The decomp's data files and code assume the EE's 32-bit pointers. Phase 1
# targets x86-32; a 64-bit build needs the 32-bit-window memory model first.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "recvx_pc must currently be built as 32-bit. "
                        "Linux: -DCMAKE_TOOLCHAIN_FILE=cmake/linux-i686.cmake; Windows: -A Win32")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)   # not -O0
endif()

# Linux: run on older glibc than the build machine's (cmake/glibc_compat.h/.c, docs/BUILDING.md "Running on older distributions (glibc)")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(RECVX_GLIBC_COMPAT "Bind glibc symbols to versions up to 2.34" ON)
    if(RECVX_GLIBC_COMPAT)
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/glibc_compat.h>")
        add_library(glibc_compat OBJECT cmake/glibc_compat.c)
        target_compile_options(glibc_compat PRIVATE -std=gnu11 -Wall)   # C11 + POSIX, not C23/_GNU_SOURCE (see the file)
    endif()
endif()

set(RECVX_DECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/decomp" CACHE PATH "recvx-decomp checkout")

enable_testing()
add_subdirectory(pc)
