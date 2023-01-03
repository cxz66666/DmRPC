project(fork_speed)

set(PROGRAMS fork_speed_cxl fork_speed_rmem)


foreach(project_name IN LISTS PROGRAMS)
    add_executable(${project_name} ${CMAKE_SOURCE_DIR}/cn/app/fork_speed/${project_name}.cpp ${APP_ADDITIONAL_SOURCES})
    target_link_libraries(${project_name} -Wl,--whole-archive erpc -Wl,--no-whole-archive rmem ${LIBRARIES} stdc++fs)
endforeach()

