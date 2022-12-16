project(img_transcode)

set(PROGRAMS client_rmem client_erpc firewall img_bitmap_worker img_server load_balance)


foreach(project_name IN LISTS PROGRAMS)
    add_executable(${project_name} ${CMAKE_SOURCE_DIR}/cn/app/img_transcode/${project_name}/${project_name}.cpp ${APP_ADDITIONAL_SOURCES})
    target_link_libraries(${project_name} -Wl,--whole-archive erpc -Wl,--no-whole-archive rmem ${LIBRARIES})
endforeach()
