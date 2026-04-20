add_library(mycrt STATIC runtime.c)
target_include_directories(mycrt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
