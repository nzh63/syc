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