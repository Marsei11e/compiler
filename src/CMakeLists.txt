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

add_library(myc_lexer STATIC)
target_sources(myc_lexer
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        lexer/lexer.cppm
    PRIVATE
        lexer/lexer.cpp
)
target_include_directories(myc_lexer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(myc_lexer PUBLIC cxx_std_23)
target_link_libraries(myc_lexer PUBLIC myc_diag)

add_library(myc_parser STATIC)
target_sources(myc_parser
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        parser/ast.cppm
        parser/ast_dump.cppm
        parser/parser.cppm
    PRIVATE
        parser/parser.cpp
        parser/ast_dump.cpp
)
target_include_directories(myc_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(myc_parser PUBLIC cxx_std_23)
target_link_libraries(myc_parser PUBLIC myc_lexer myc_diag)

add_library(myc_sema STATIC)
target_sources(myc_sema
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        sema/sema.cppm
    PRIVATE
        sema/sema.cpp
        sema/expr_check.cpp
        sema/overload.cpp
        sema/desugar.cpp
        sema/cflow_check.cpp
        sema/move_check.cpp
        sema/effect.cpp
)
target_include_directories(myc_sema PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(myc_sema PUBLIC cxx_std_23)
target_link_libraries(myc_sema PUBLIC myc_parser myc_lexer myc_diag)

add_library(myc_ir STATIC)
target_sources(myc_ir
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        ir/ir.cppm
    PRIVATE
        ir/lowering.cpp
        ir/dump.cpp
        ir/opt/const_fold.cpp
        ir/opt/dce.cpp
)
target_include_directories(myc_ir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(myc_ir PUBLIC cxx_std_23)
target_link_libraries(myc_ir PUBLIC myc_sema myc_parser myc_lexer myc_diag)

add_library(myc_codegen STATIC)
target_sources(myc_codegen
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        codegen/codegen.cppm
    PRIVATE
        codegen/llvm_types.cpp
        codegen/mangle.cpp
        codegen/llvm_emitter.cpp
)
target_include_directories(myc_codegen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(myc_codegen PUBLIC cxx_std_23)
target_link_libraries(myc_codegen PUBLIC myc_ir myc_sema myc_parser myc_lexer myc_diag)

add_executable(myc main.cpp)
target_link_libraries(myc PRIVATE mycrt myc_codegen myc_ir myc_sema myc_parser)
target_include_directories(myc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(myc PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mycc_test
    tests/unit/test_diag.cpp
)
target_link_libraries(mycc_test PRIVATE myc_diag)
target_include_directories(mycc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test PRIVATE -Wall -Wextra)

set(PARTICLES_LANG "${CMAKE_SOURCE_DIR}/readit/particles.lang")
add_executable(mycc_test_lexer
    tests/unit/test_lexer.cpp
)
target_link_libraries(mycc_test_lexer PRIVATE myc_lexer)
target_include_directories(mycc_test_lexer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_lexer PRIVATE -Wall -Wextra)

add_executable(mycc_test_ast
    tests/unit/test_ast.cpp
)
target_link_libraries(mycc_test_ast PRIVATE myc_parser)
target_include_directories(mycc_test_ast PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_ast PRIVATE -Wall -Wextra)

add_executable(mycc_test_parser
    tests/unit/test_parser.cpp
)
target_link_libraries(mycc_test_parser PRIVATE myc_parser)
target_include_directories(mycc_test_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_parser PRIVATE -Wall -Wextra)

add_executable(mycc_test_s6
    tests/unit/test_s6.cpp
)
target_link_libraries(mycc_test_s6 PRIVATE myc_parser)
target_include_directories(mycc_test_s6 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s6 PRIVATE -Wall -Wextra)

add_executable(mycc_test_s7
    tests/unit/test_s7.cpp
)
target_link_libraries(mycc_test_s7 PRIVATE myc_sema)
target_include_directories(mycc_test_s7 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s7 PRIVATE -Wall -Wextra)

set(TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_executable(mycc_test_s8
    tests/unit/test_s8.cpp
)
target_link_libraries(mycc_test_s8 PRIVATE myc_sema)
target_include_directories(mycc_test_s8 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s8 PRIVATE -Wall -Wextra)
add_test(NAME s8 COMMAND mycc_test_s8 ${TESTS_DIR})

add_executable(mycc_test_s9
    tests/unit/test_s9.cpp
)
target_link_libraries(mycc_test_s9 PRIVATE myc_sema)
target_include_directories(mycc_test_s9 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s9 PRIVATE -Wall -Wextra)
add_test(NAME s9 COMMAND mycc_test_s9 ${TESTS_DIR})

add_executable(mycc_test_s10
    tests/unit/test_s10.cpp
)
target_link_libraries(mycc_test_s10 PRIVATE myc_sema)
target_include_directories(mycc_test_s10 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s10 PRIVATE -Wall -Wextra)
add_test(NAME s10 COMMAND mycc_test_s10 ${TESTS_DIR})

add_executable(mycc_test_s11
    tests/unit/test_s11.cpp
)
target_link_libraries(mycc_test_s11 PRIVATE myc_sema)
target_include_directories(mycc_test_s11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s11 PRIVATE -Wall -Wextra)
add_test(NAME s11 COMMAND mycc_test_s11 ${TESTS_DIR})

add_executable(mycc_test_s12
    tests/unit/test_s12.cpp
)
target_link_libraries(mycc_test_s12 PRIVATE myc_sema)
target_include_directories(mycc_test_s12 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s12 PRIVATE -Wall -Wextra)
add_test(NAME s12 COMMAND mycc_test_s12 ${TESTS_DIR})

add_executable(mycc_test_s13
    tests/unit/test_s13.cpp
)
target_link_libraries(mycc_test_s13 PRIVATE myc_sema)
target_include_directories(mycc_test_s13 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s13 PRIVATE -Wall -Wextra)
add_test(NAME s13 COMMAND mycc_test_s13 ${TESTS_DIR})

add_executable(mycc_test_s14
    tests/unit/test_s14.cpp
)
target_link_libraries(mycc_test_s14 PRIVATE myc_ir myc_sema myc_parser)
target_include_directories(mycc_test_s14 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s14 PRIVATE -Wall -Wextra)
add_test(NAME s14 COMMAND mycc_test_s14 ${TESTS_DIR})

add_executable(mycc_test_s15
    tests/unit/test_s15.cpp
)
target_link_libraries(mycc_test_s15 PRIVATE myc_ir myc_sema myc_parser)
target_include_directories(mycc_test_s15 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s15 PRIVATE -Wall -Wextra)
add_test(NAME s15 COMMAND mycc_test_s15 ${TESTS_DIR})

add_executable(mycc_test_s16
    tests/unit/test_s16.cpp
)
target_link_libraries(mycc_test_s16 PRIVATE myc_codegen myc_ir myc_sema myc_parser)
target_include_directories(mycc_test_s16 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s16 PRIVATE -Wall -Wextra)
add_test(NAME s16 COMMAND mycc_test_s16)

add_executable(mycc_test_s17
    tests/unit/test_s17.cpp
)
target_link_libraries(mycc_test_s17 PRIVATE myc_codegen myc_ir myc_sema myc_parser)
target_include_directories(mycc_test_s17 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s17 PRIVATE -Wall -Wextra)
add_test(NAME s17 COMMAND mycc_test_s17 ${TESTS_DIR})

add_executable(mycc_test_s18
    tests/unit/test_s18.cpp
)
target_link_libraries(mycc_test_s18 PRIVATE mycrt)
target_include_directories(mycc_test_s18 PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(mycc_test_s18 PRIVATE -Wall -Wextra)
add_test(NAME s18 COMMAND mycc_test_s18)

add_executable(mycc_test_s19
    tests/unit/test_s19.cpp
)
target_link_libraries(mycc_test_s19 PRIVATE myc_codegen myc_ir myc_sema myc_parser)
target_include_directories(mycc_test_s19 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s19 PRIVATE -Wall -Wextra)
add_test(NAME s19 COMMAND mycc_test_s19 ${TESTS_DIR})

add_executable(mycc_test_s20
    tests/unit/test_s20.cpp
    )
target_link_libraries(mycc_test_s20 PRIVATE myc_codegen myc_ir myc_sema myc_parser)
target_include_directories(mycc_test_s20 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mycc_test_s20 PRIVATE -Wall -Wextra)
add_test(NAME s20 COMMAND mycc_test_s20)

add_test(NAME diag   COMMAND mycc_test)
add_test(NAME lexer  COMMAND mycc_test_lexer ${PARTICLES_LANG})
add_test(NAME ast    COMMAND mycc_test_ast)
add_test(NAME parser COMMAND mycc_test_parser)
add_test(NAME s6     COMMAND mycc_test_s6 ${TESTS_DIR})
add_test(NAME s7     COMMAND mycc_test_s7)
