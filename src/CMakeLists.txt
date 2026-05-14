add_library(myc_diag STATIC)
target_sources(myc_diag
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        diag/diag.cppm
    PRIVATE
        diag/source.cpp
        diag/diagnostic.cpp
)
target_include_directories(myc_diag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(myc_diag PUBLIC cxx_std_23)

set(LEXER_SOURCES
    lexer/lexer.cpp
)

set(PARSER_SOURCES
    parser/ast_dump.cpp
    parser/parser.cpp
)

set(SEMA_SOURCES
    sema/sema.cpp
    sema/expr_check.cpp
    sema/overload.cpp
    sema/desugar.cpp
    sema/cflow_check.cpp
    sema/move_check.cpp
    sema/effect.cpp
)

set(IR_SOURCES
    ir/lowering.cpp
    ir/dump.cpp
    ir/opt/const_fold.cpp
    ir/opt/dce.cpp
)

add_executable(myc main.cpp ${LEXER_SOURCES} ${PARSER_SOURCES} ${SEMA_SOURCES} ${IR_SOURCES})
target_link_libraries(myc PRIVATE mycrt myc_diag)
target_include_directories(myc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(myc PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Unit test targets
add_executable(mycc_test
    ../tests/unit/test_diag.cpp
)
target_link_libraries(mycc_test PRIVATE myc_diag)
target_include_directories(mycc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test PRIVATE -Wall -Wextra)

set(PARTICLES_LANG "${CMAKE_SOURCE_DIR}/readit/particles.lang")
add_executable(mycc_test_lexer
    ../tests/unit/test_lexer.cpp
    ${LEXER_SOURCES}
)
target_link_libraries(mycc_test_lexer PRIVATE myc_diag)
target_include_directories(mycc_test_lexer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_lexer PRIVATE -Wall -Wextra)

add_executable(mycc_test_ast
    ../tests/unit/test_ast.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
)
target_link_libraries(mycc_test_ast PRIVATE myc_diag)
target_include_directories(mycc_test_ast PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_ast PRIVATE -Wall -Wextra)

add_executable(mycc_test_parser
    ../tests/unit/test_parser.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
)
target_link_libraries(mycc_test_parser PRIVATE myc_diag)
target_include_directories(mycc_test_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_parser PRIVATE -Wall -Wextra)

add_executable(mycc_test_s6
    ../tests/unit/test_s6.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
)
target_link_libraries(mycc_test_s6 PRIVATE myc_diag)
target_include_directories(mycc_test_s6 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s6 PRIVATE -Wall -Wextra)

add_executable(mycc_test_s7
    ../tests/unit/test_s7.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
)
target_link_libraries(mycc_test_s7 PRIVATE myc_diag)
target_include_directories(mycc_test_s7 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s7 PRIVATE -Wall -Wextra)

set(TESTS_DIR "${CMAKE_SOURCE_DIR}/readit/tests")
add_executable(mycc_test_s8
    ../tests/unit/test_s8.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
)
target_link_libraries(mycc_test_s8 PRIVATE myc_diag)
target_include_directories(mycc_test_s8 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s8 PRIVATE -Wall -Wextra)
add_test(NAME s8 COMMAND mycc_test_s8 ${TESTS_DIR})

add_executable(mycc_test_s9
    ../tests/unit/test_s9.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
)
target_link_libraries(mycc_test_s9 PRIVATE myc_diag)
target_include_directories(mycc_test_s9 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s9 PRIVATE -Wall -Wextra)
add_test(NAME s9 COMMAND mycc_test_s9 ${TESTS_DIR})

add_executable(mycc_test_s10
    ../tests/unit/test_s10.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
)
target_link_libraries(mycc_test_s10 PRIVATE myc_diag)
target_include_directories(mycc_test_s10 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s10 PRIVATE -Wall -Wextra)
add_test(NAME s10 COMMAND mycc_test_s10 ${TESTS_DIR})

add_executable(mycc_test_s11
    ../tests/unit/test_s11.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
)
target_link_libraries(mycc_test_s11 PRIVATE myc_diag)
target_include_directories(mycc_test_s11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s11 PRIVATE -Wall -Wextra)
add_test(NAME s11 COMMAND mycc_test_s11 ${TESTS_DIR})

add_executable(mycc_test_s12
    ../tests/unit/test_s12.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
)
target_link_libraries(mycc_test_s12 PRIVATE myc_diag)
target_include_directories(mycc_test_s12 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s12 PRIVATE -Wall -Wextra)
add_test(NAME s12 COMMAND mycc_test_s12 ${TESTS_DIR})

add_executable(mycc_test_s13
    ../tests/unit/test_s13.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
)
target_link_libraries(mycc_test_s13 PRIVATE myc_diag)
target_include_directories(mycc_test_s13 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s13 PRIVATE -Wall -Wextra)
add_test(NAME s13 COMMAND mycc_test_s13 ${TESTS_DIR})

add_executable(mycc_test_s14
    ../tests/unit/test_s14.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
    ${IR_SOURCES}
)
target_link_libraries(mycc_test_s14 PRIVATE myc_diag)
target_include_directories(mycc_test_s14 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s14 PRIVATE -Wall -Wextra)
add_test(NAME s14 COMMAND mycc_test_s14 ${TESTS_DIR})

add_executable(mycc_test_s15
    ../tests/unit/test_s15.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${SEMA_SOURCES}
    ${IR_SOURCES}
)
target_link_libraries(mycc_test_s15 PRIVATE myc_diag)
target_include_directories(mycc_test_s15 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s15 PRIVATE -Wall -Wextra)
add_test(NAME s15 COMMAND mycc_test_s15 ${TESTS_DIR})
