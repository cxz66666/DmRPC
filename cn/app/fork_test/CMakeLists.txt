project(fork_test)

set(PROGRAMS_RMEM fork_test_rmem_client fork_test_rmem_server)
set(PROGRAMS_CXL fork_test_cxl_client fork_test_cxl_server)


foreach(project_name IN LISTS PROGRAMS_RMEM)
    add_executable(${project_name} ${CMAKE_SOURCE_DIR}/cn/app/fork_test/fork_test_rmem/${project_name}.cpp ${APP_ADDITIONAL_SOURCES})
    target_link_libraries(${project_name} -Wl,--whole-archive erpc -Wl,--no-whole-archive rmem ${LIBRARIES})
endforeach()

foreach(project_name IN LISTS PROGRAMS_CXL)
    add_executable(${project_name} ${CMAKE_SOURCE_DIR}/cn/app/fork_test/fork_test_cxl/${project_name}.cpp ${APP_ADDITIONAL_SOURCES})
    target_link_libraries(${project_name} -Wl,--whole-archive erpc -Wl,--no-whole-archive rmem ${LIBRARIES} stdc++fs)
endforeach()
