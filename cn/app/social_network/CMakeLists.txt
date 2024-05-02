project(social_network)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)


set(PROGRAMS load_balance unique_id url_shorten nginx user_mention user_timeline post_storage home_timeline user_service compose_post client)

set(APP_ADDITIONAL_SOURCES ${CMAKE_SOURCE_DIR}/cn/app/social_network/social_network.pb.cc)

find_package (mongoc-1.0 1.7 REQUIRED)
# include directories for the mongoc driver, which is very weird in https://mongoc.org/libmongoc/1.22.0/tutorial.html
include_directories(/usr/local/include/libmongoc-1.0 /usr/local/include/libbson-1.0)

set(LIBRARIES ${LIBRARIES} mongo::mongoc_static)

if(SOCIAL_NETWORK_TYPE STREQUAL "erpc")
    add_definitions(-DERPC_PROGRAM)
    message(STATUS "Using erpc for social_network")
elseif (SOCIAL_NETWORK_TYPE STREQUAL "rmem")
    add_definitions(-DRMEM_PROGRAM)
    message(STATUS "Using rmem for social_network")
else()
    message(STATUS "ERROR for COMPILE TYPE")
endif ()

foreach (project_name IN LISTS PROGRAMS)
    add_executable(${project_name} ${CMAKE_SOURCE_DIR}/cn/app/social_network/${project_name}/${project_name}.cpp ${APP_ADDITIONAL_SOURCES})
    target_link_libraries(${project_name} -Wl,--whole-archive erpc -Wl,--no-whole-archive rmem ${LIBRARIES})
endforeach ()