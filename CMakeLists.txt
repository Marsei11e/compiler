cmake_minimum_required(VERSION 3.28)
project(myc VERSION 0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_subdirectory(runtime)
add_subdirectory(src)
