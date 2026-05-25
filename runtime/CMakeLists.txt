add_library(mycrt STATIC runtime.c)
target_include_directories(mycrt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mycrt PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_compile_options(mycrt PRIVATE -Wall -Wextra -Wpedantic)
