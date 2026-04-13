aux_source_directory(. DIR_LIB_SRCS)
add_library (base ${DIR_LIB_SRCS})

add_executable(test_log TestLog.cpp)
target_link_libraries(test_log base)
