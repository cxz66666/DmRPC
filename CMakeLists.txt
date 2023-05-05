cmake_minimum_required(VERSION 3.0)
project(rMEM)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PERF ON)

# Parse the build type
if(PERF)
  message(STATUS "Compilation optimized for performance.")
  message(STATUS "Debugging is disabled.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
  set(TESTING OFF)
else()
  message(STATUS "Compilation not optimized for performance.")
  message(STATUS "Debugging is enabled. Perf will be low.")
  set(CMAKE_BUILD_TYPE Debug)
  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")

  # For now, disable unit tests on Windows
  if (NOT WIN32)
    set(TESTING ON)
  endif()
endif(PERF)

find_package (mongoc-1.0 1.7 REQUIRED)

set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf REQUIRED)
if(Protobuf_FOUND)
  message(STATUS "Using Protocol Buffers ${Protobuf_VERSION}")
#  message(STATUS "${Protobuf_INCLUDE_DIRS}")
#  message(STATUS "${Protobuf_LITE_LIBRARIES}")
endif()


# build user lib to add include dir
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/util ${CMAKE_SOURCE_DIR}/cn/rmem_ulib ${CMAKE_SOURCE_DIR}/cn/app ${CMAKE_SOURCE_DIR}/mn)

# include directories for the mongoc driver, which is very weird in https://mongoc.org/libmongoc/1.22.0/tutorial.html
include_directories(/usr/local/include/libmongoc-1.0 /usr/local/include/libbson-1.0)
# include directories for the protobuf library
include_directories(${Protobuf_INCLUDE_DIRS})

# Common sub-projects: asio 
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party/asio/include) 

include_directories(${CMAKE_SOURCE_DIR}/third_party/eRPC/src)

# Common sub-projects: atomic_queue
include_directories(${CMAKE_SOURCE_DIR}/third_party/atomic_queue/include)

# Common sub-projects: parallel_hashmap
include_directories(${CMAKE_SOURCE_DIR}/third_party/parallel_hashmap)

# Common sub-projects: HdrHistogram_c
include_directories(${CMAKE_SOURCE_DIR}/third_party/HdrHistogram_c/include)

# Common sub-projects: json
include_directories(${CMAKE_SOURCE_DIR}/third_party/json/include)

# Add additional compilation flags only after adding subprojects
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DALLOW_EXPERIMENTAL_API -std=c++11 -march=native -Wall -Wextra -Werror -pedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wold-style-cast -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types -Wno-keyword-macro -Wno-deprecated-declarations")



set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)


# DPDK may be needed for different reasons (e.g., the transport is DPDK, or
# if the transport is not DPDK but the application needs DPDK libraries)

# Options exposed to the user
option(PERF "Compile for performance" ON)
set(PGO "none" CACHE STRING "Profile-guided optimization (generate/use/none)")
set(RMEM_LOG_LEVEL "info" CACHE  STRING "Logging level (none/error/warn/info/reorder/trace/cc)") 

set(IMG_TRANSCODE_TYPE "rmem" CACHE STRING "Image transcode type (erpc/rmem/cxl)")
if(EXISTS "${CMAKE_SOURCE_DIR}/cn/app/img_transcode/build_app")
  file(STRINGS "${CMAKE_SOURCE_DIR}/cn/app/img_transcode/build_app" IMG_TRANSCODE_TYPE_FILE)
  if(IMG_TRANSCODE_TYPE_FILE STREQUAL "erpc")
    set(IMG_TRANSCODE_TYPE "erpc")
  elseif(IMG_TRANSCODE_TYPE_FILE STREQUAL "rmem")
    set(IMG_TRANSCODE_TYPE "rmem")
  elseif(IMG_TRANSCODE_TYPE_FILE STREQUAL "cxl")
    set(IMG_TRANSCODE_TYPE "cxl")
  elseif(IMG_TRANSCODE_TYPE_FILE STREQUAL "lb_test")
    set(IMG_TRANSCODE_TYPE "lb_test")
  endif()
endif()

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
if(RMEM_LOG_LEVEL STREQUAL "none")
  message(STATUS "RMEM Logging level = none.")
  add_definitions(-DRMEM_LOG_LEVEL=0)
elseif(RMEM_LOG_LEVEL STREQUAL "error")
  message(STATUS "RMEM Logging level = error.")
  add_definitions(-DRMEM_LOG_LEVEL=1)
elseif(RMEM_LOG_LEVEL STREQUAL "warn")
  message(STATUS "RMEM Logging level = warn.")
  add_definitions(-DRMEM_LOG_LEVEL=2)
elseif(RMEM_LOG_LEVEL STREQUAL "info")
  message(STATUS "RMEM Logging level = info.")
  add_definitions(-DRMEM_LOG_LEVEL=3)
elseif(RMEM_LOG_LEVEL STREQUAL "reorder")
  message(STATUS "RMEM Logging level = reorder. Warning: Performance will be low.")
  add_definitions(-DRMEM_LOG_LEVEL=4)
elseif(RMEM_LOG_LEVEL STREQUAL "trace")
  message(STATUS "RMEM Logging level = trace. Trace, on! Warning: Performance will be low.")
  add_definitions(-DRMEM_LOG_LEVEL=5)
elseif(RMEM_LOG_LEVEL STREQUAL "cc")
  message(STATUS "RMEM Logging level = cc. Warning: Performance will be low.")
  add_definitions(-DRMEM_LOG_LEVEL=6)
else()
  message(STATUS "No logging level specified. Using warning level.")
  add_definitions(-DRMEM_LOG_LEVEL=2)
endif()

# Testing for packet loss, machine failure, etc
if(TESTING)
  message(STATUS "Testing is enabled. Performance will be low.")
  add_definitions(-DRMEM_TESTING=true)
else(TESTING)
  message(STATUS "Testing is disabled, so tests may fail.")
  add_definitions(-DRMEM_TESTING=false)
endif(TESTING)


# The app to compile. Only one app is compiled to reduce compile time.
if(EXISTS "${CMAKE_SOURCE_DIR}/cn/app/build_app")
  file(STRINGS "${CMAKE_SOURCE_DIR}/cn/app/build_app" APP)
else()
  message(STATUS "No build_app found. No application will be compiled.")
  return()
endif()
message(STATUS "Compiling app = " ${APP})

set(UTILSOURCES
        ${CMAKE_SOURCE_DIR}/util/ring_buf.cpp
        ${CMAKE_SOURCE_DIR}/util/numautil.cpp
  )


set(LIBSOURCES
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/api.cpp 
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/concurrent_store.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/condition_resp.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/context.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/extern.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/worker.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/worker_store.cpp
  ${CMAKE_SOURCE_DIR}/cn/rmem_ulib/impl/api.cpp
  ${UTILSOURCES}
  )

set(
      MNSOURCES
        ${CMAKE_SOURCE_DIR}/mn/impl/gflag_configs.cpp
        ${CMAKE_SOURCE_DIR}/mn/impl/page.cpp
        ${CMAKE_SOURCE_DIR}/mn/impl/req_handler.cpp
        ${CMAKE_SOURCE_DIR}/mn/impl/server.cpp
        ${CMAKE_SOURCE_DIR}/mn/impl/server_context.cpp
        ${CMAKE_SOURCE_DIR}/mn/impl/mm_struct.cpp
        ${CMAKE_SOURCE_DIR}/mn/impl/server_extern.cpp
        ${UTILSOURCES}
)

# --export-dynamic used for backtrace_symbols()

set(LIBRARIES ${LIBRARIES} -Wl,--export-dynamic -Wl,--whole-archive -Wl,--as-needed rte_node rte_graph rte_bpf rte_flow_classify rte_pipeline rte_table rte_port rte_fib rte_ipsec rte_vhost rte_stack rte_security rte_sched rte_reorder rte_rib rte_regexdev rte_rawdev rte_pdump rte_power rte_member rte_lpm rte_latencystats rte_kni rte_jobstats rte_ip_frag rte_gso rte_gro rte_eventdev rte_efd rte_distributor rte_cryptodev rte_compressdev rte_cfgfile rte_bitratestats rte_bbdev rte_acl rte_timer rte_hash rte_metrics rte_cmdline rte_pci rte_ethdev rte_meter rte_net rte_mbuf rte_mempool rte_rcu rte_ring rte_eal rte_telemetry rte_kvargs -Wl,--no-whole-archive numa dl ibverbs mlx5)
set(LIBRARIES ${LIBRARIES} gflags pthread hdr_histogram_static nlohmann_json::nlohmann_json mongo::mongoc_static ${Protobuf_LITE_LIBRARIES})


add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/eRPC)

add_subdirectory(third_party/HdrHistogram_c)

add_subdirectory(third_party/json)

# Compile the library
add_library(rmem ${LIBSOURCES})

add_executable(rmem_mn ${MNSOURCES})

add_dependencies(rmem erpc)


add_dependencies(rmem_mn erpc)

target_link_libraries(rmem_mn erpc ${LIBRARIES})


# Using link-time optimization sometimes requires building with sources instead
# of liberpc. See the hello world example's Makefile for an example of
# compiling with liberpc.

if(APP STREQUAL "img_transcode")
  add_subdirectory(${CMAKE_SOURCE_DIR}/cn/app/img_transcode)
elseif(APP STREQUAL "social_network")
  add_subdirectory(${CMAKE_SOURCE_DIR}/cn/app/social_network)
elseif(APP STREQUAL "fork_test")
  add_subdirectory(${CMAKE_SOURCE_DIR}/cn/app/fork_test)
elseif(APP STREQUAL "fork_speed")
  add_subdirectory(${CMAKE_SOURCE_DIR}/cn/app/fork_speed)
else()
  add_executable(${APP} ${CMAKE_SOURCE_DIR}/cn/app/${APP}/${APP}.cpp ${APP_ADDITIONAL_SOURCES})
  target_link_libraries(${APP} -Wl,--whole-archive erpc -Wl,--no-whole-archive rmem ${LIBRARIES})
endif()


