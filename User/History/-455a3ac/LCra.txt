aux_source_directory(. SRCS)
add_executable(tmms ${SRCS})

add_dependencies(tmms base jsoncpp)
target_link_libraries(tmms base ${PROJECT_SOURCE_DIR}/lib/jsoncpp/lib64/libjsoncpp.a)
install(TARGETS tmms RUNTIME DESTINATION sbin)
