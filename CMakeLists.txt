cmake_minimum_required(VERSION 3.0)
project(rMEM)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PERF ON CACHE BOOL "Datapath transport (infiniband/raw/dpdk/fake)")

# Parse the build type
if(PERF)
  message(STATUS "Compilation optimized for performance.")
  message(STATUS "Debugging is disabled.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
  set(TESTING OFF)
else(PERF)
  message(STATUS "Compilation not optimized for performance.")
  message(STATUS "Debugging is enabled. Perf will be low.")
  set(CMAKE_BUILD_TYPE Debug)
  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")

  # For now, disable unit tests on Windows
  if (NOT WIN32)
    set(TESTING ON)
  endif()
endif(PERF)


# build user lib to add include dir
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/util ${CMAKE_SOURCE_DIR}/cn/rmem_ulib)


# Common sub-projects: asio 
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party/asio/include) 

include_directories(${CMAKE_SOURCE_DIR}/third_party/eRPC/src)


# Add additional compilation flags only after adding subprojects
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DALLOW_EXPERIMENTAL_API -std=c++11 -march=native -Wall -Wextra -Werror -pedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-conversion -Wold-style-cast -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types -Wno-keyword-macro -Wno-deprecated-declarations")



set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# DPDK may be needed for different reasons (e.g., the transport is DPDK, or
# if the transport is not DPDK but the application needs DPDK libraries)

# Options exposed to the user
option(PERF "Compile for performance" ON)
set(PGO "none" CACHE STRING "Profile-guided optimization (generate/use/none)")
set(LOG_LEVEL "warn" CACHE STRING "Logging level (none/error/warn/info/reorder/trace/cc)") 

# Profile-guided optimization
if(PGO STREQUAL "generate")
  message(STATUS "Profile-guided optimization (generate mode) is enabled. Performance will be low.")
  add_definitions(-fprofile-generate)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
elseif(PGO STREQUAL "use")
  message(STATUS "Profile-guided optimization (use mode) is enabled.")
  add_definitions(-fprofile-use -fprofile-correction)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use -fprofile-correction")
elseif(PGO STREQUAL "none")
  message(STATUS "Profile-guided optimization is disabled.")
endif()

# Logging level
if(LOG_LEVEL STREQUAL "none")
  message(STATUS "Logging level = none.")
  add_definitions(-DERPC_LOG_LEVEL=0)
elseif(LOG_LEVEL STREQUAL "error")
  message(STATUS "Logging level = error.")
  add_definitions(-DERPC_LOG_LEVEL=1)
elseif(LOG_LEVEL STREQUAL "warn")
  message(STATUS "Logging level = warn.")
  add_definitions(-DERPC_LOG_LEVEL=2)
elseif(LOG_LEVEL STREQUAL "info")
  message(STATUS "Logging level = info.")
  add_definitions(-DERPC_LOG_LEVEL=3)
elseif(LOG_LEVEL STREQUAL "reorder")
  message(STATUS "Logging level = reorder. Warning: Performance will be low.")
  add_definitions(-DERPC_LOG_LEVEL=4)
elseif(LOG_LEVEL STREQUAL "trace")
  message(STATUS "Logging level = trace. Trace, on! Warning: Performance will be low.")
  add_definitions(-DERPC_LOG_LEVEL=5)
elseif(LOG_LEVEL STREQUAL "cc")
  message(STATUS "Logging level = cc. Warning: Performance will be low.")
  add_definitions(-DERPC_LOG_LEVEL=6)
else()
  message(STATUS "No logging level specified. Using warning level.")
  add_definitions(-DERPC_LOG_LEVEL=2)
endif()

# Testing for packet loss, machine failure, etc
if(TESTING)
  message(STATUS "Testing is enabled. Performance will be low.")
  add_definitions(-DERPC_TESTING=true)
else(TESTING)
  message(STATUS "Testing is disabled, so tests may fail.")
  add_definitions(-DERPC_TESTING=false)
endif(TESTING)

set(LIBSOURCES
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/api.cpp 
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/concurrent_store.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/condition_resp.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/context.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/extern.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/worker.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/api.cpp
  ${CMAKE_SOURCE_DIR}/util/ring_buf.cpp
  ${CMAKE_SOURCE_DIR}/util/numautil.cpp

  )





# Compile the library
add_library(rmem ${LIBSOURCES})

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/eRPC)