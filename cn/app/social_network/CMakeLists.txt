project(social_network)

set(PROGRAMS load_balance unique_id url_shorten nginx user_mention user_timeline post_storage home_timeline user_service media)

set(APP_ADDITIONAL_SOURCES ${CMAKE_SOURCE_DIR}/cn/app/social_network/social_network.pb.cc)

add_definitions(-DERPC_PROGRAM)

foreach (project_name IN LISTS PROGRAMS)
    add_executable(${project_name} ${CMAKE_SOURCE_DIR}/cn/app/social_network/${project_name}/${project_name}.cpp ${APP_ADDITIONAL_SOURCES})
    target_link_libraries(${project_name} -Wl,--whole-archive erpc -Wl,--no-whole-archive rmem ${LIBRARIES})
endforeach ()