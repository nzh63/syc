cmake_minimum_required(VERSION 3.0.0)
project(syc VERSION 0.1.0)

find_package(BISON)
find_package(FLEX)
BISON_TARGET(Parser src/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp)
FLEX_TARGET(Scanner src/tokens.l  ${CMAKE_CURRENT_BINARY_DIR}/tokens.cpp)
ADD_FLEX_BISON_DEPENDENCY(Scanner Parser)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(src)

file(GLOB srcs src/*.cpp)

add_executable(syc
    ${srcs}
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Scanner_OUTPUTS}
)

add_custom_command(OUTPUT test_task
                   COMMAND node ${PROJECT_SOURCE_DIR}/test/test.js
                   DEPENDS syc
                   VERBATIM
)
add_custom_target(test DEPENDS test_task)

add_custom_command(OUTPUT test_performance_task
                   COMMAND node ${PROJECT_SOURCE_DIR}/test/test_performance.js
                   DEPENDS syc
                   VERBATIM
)
add_custom_target(test_performance DEPENDS test_performance_task)
