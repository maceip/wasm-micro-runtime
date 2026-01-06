# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.14)

include(CheckPIESupported)

if (NOT WAMR_BUILD_PLATFORM STREQUAL "windows")
  project (cpp-mcp-server C CXX ASM)
else()
  project (cpp-mcp-server C CXX ASM)
endif()

################  runtime settings  ################
string (TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if (APPLE)
  add_definitions(-DBH_PLATFORM_DARWIN)
endif ()

# Reset default linker flags
set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

# WAMR features switch

# Set WAMR_BUILD_TARGET, currently values supported:
# "X86_64", "AMD_64", "X86_32", "AARCH64[sub]", "ARM[sub]", "THUMB[sub]",
# "MIPS", "XTENSA", "RISCV64[sub]", "RISCV32[sub]"
if (NOT DEFINED WAMR_BUILD_TARGET)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
    set (WAMR_BUILD_TARGET "AARCH64")
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    set (WAMR_BUILD_TARGET "RISCV64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    # Build as X86_64 by default in 64-bit platform
    set (WAMR_BUILD_TARGET "X86_64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Build as X86_32 by default in 32-bit platform
    set (WAMR_BUILD_TARGET "X86_32")
  else ()
    message(SEND_ERROR "Unsupported build target platform!")
  endif ()
endif ()

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Release)
endif ()

# Set C++ standard
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

set (WAMR_BUILD_INTERP 1)
set (WAMR_BUILD_AOT 1)
set (WAMR_BUILD_JIT 0)
set (WAMR_BUILD_LIBC_BUILTIN 1)

if (NOT MSVC)
  set (WAMR_BUILD_LIBC_WASI 1)
endif ()

if (NOT MSVC)
  # linker flags
  if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
  endif ()
  if (WAMR_BUILD_TARGET MATCHES "X86_.*" OR WAMR_BUILD_TARGET STREQUAL "AMD_64")
    if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
      set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mindirect-branch-register")
    endif ()
  endif ()
endif ()

# Find required packages
find_package(Threads REQUIRED)

# build out vmlib
set (WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

################  MCP library  ################
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/common)

# Build MCP library
set(TARGET mcp)
add_library(${TARGET} STATIC
    include/mcp_client.h
    src/mcp_message.cpp
    include/mcp_message.h
    src/mcp_resource.cpp
    include/mcp_resource.h
    src/mcp_server.cpp
    include/mcp_server.h
    src/mcp_tool.cpp
    include/mcp_tool.h
    src/mcp_stdio_client.cpp
    include/mcp_stdio_client.h
    src/mcp_sse_client.cpp
    include/mcp_sse_client.h
)

target_link_libraries(${TARGET} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

################  application related  ################
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)
include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

add_executable (cpp-mcp-server
    src/main.cpp
    ${UNCOMMON_SHARED_SOURCE}
)

add_executable (cpp-mcp-server-oauth
    src/main_oauth.cpp
    ${UNCOMMON_SHARED_SOURCE}
)

# Include OAuth handler header directory
target_include_directories(cpp-mcp-server-oauth PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

check_pie_supported()
set_target_properties (cpp-mcp-server PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties (cpp-mcp-server-oauth PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (APPLE)
  target_link_libraries (cpp-mcp-server vmlib mcp -lm -ldl -lpthread)
  target_link_libraries (cpp-mcp-server-oauth vmlib mcp -lm -ldl -lpthread)
else ()
  target_link_libraries (cpp-mcp-server vmlib mcp -lm -ldl -lpthread -lrt)
  target_link_libraries (cpp-mcp-server-oauth vmlib mcp -lm -ldl -lpthread -lrt)
endif ()
