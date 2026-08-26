cmake_minimum_required(VERSION 3.0.0)
project(syc VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/parser)

if(WIN32)
    # Use the vendored WinFlexBison binaries on Windows; download them
    set(WINFLEXBISON_DIR ${PROJECT_SOURCE_DIR}/external/winflexbison)
    if(NOT EXISTS ${WINFLEXBISON_DIR}/win_bison.exe)
        set(WFB_VERSION 2.5.24)
        set(WFB_ZIP ${CMAKE_BINARY_DIR}/win_flex_bison-${WFB_VERSION}.zip)
        set(WFB_URLS
            "https://github.com/lexxmark/winflexbison/releases/download/v${WFB_VERSION}/win_flex_bison-${WFB_VERSION}.zip")
        foreach(WFB_URL ${WFB_URLS})
            message(STATUS "WinFlexBison not found, downloading from ${WFB_URL}")
            file(DOWNLOAD ${WFB_URL} ${WFB_ZIP} STATUS WFB_STATUS
                 TIMEOUT 300 SHOW_PROGRESS)
            list(GET WFB_STATUS 0 WFB_ERR)
            if(WFB_ERR EQUAL 0)
                break()
            endif()
        endforeach()
        if(NOT WFB_ERR EQUAL 0)
            message(FATAL_ERROR
                "Failed to download WinFlexBison (${WFB_STATUS}). "
                "Please download win_flex_bison-${WFB_VERSION}.zip "
                "and extract it into ${WINFLEXBISON_DIR}.")
        endif()
        file(MAKE_DIRECTORY ${WINFLEXBISON_DIR})
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf ${WFB_ZIP}
                        WORKING_DIRECTORY ${WINFLEXBISON_DIR}
                        RESULT_VARIABLE WFB_EXTRACT_RESULT)
        file(REMOVE ${WFB_ZIP})
        if(NOT WFB_EXTRACT_RESULT EQUAL 0 OR
           NOT EXISTS ${WINFLEXBISON_DIR}/win_bison.exe)
            message(FATAL_ERROR
                "Failed to extract WinFlexBison into ${WINFLEXBISON_DIR}.")
        endif()
    endif()
    if(NOT EXISTS ${WINFLEXBISON_DIR}/win_bison.exe)
        message(FATAL_ERROR
            "${WINFLEXBISON_DIR}/win_bison.exe not found. "
            "Run tools/get-winflexbison.sh first.")
    endif()
    set(WIN_BISON_EXE ${WINFLEXBISON_DIR}/win_bison.exe)
    set(WIN_FLEX_EXE ${WINFLEXBISON_DIR}/win_flex.exe)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/parser/parser.cpp
                              ${CMAKE_CURRENT_BINARY_DIR}/parser/parser.hpp
                       COMMAND ${WIN_BISON_EXE} -d -o ${CMAKE_CURRENT_BINARY_DIR}/parser/parser.cpp
                               ${PROJECT_SOURCE_DIR}/src/parser/parser.y
                       DEPENDS src/parser/parser.y
                       VERBATIM)
    set(BISON_Parser_OUTPUTS ${CMAKE_CURRENT_BINARY_DIR}/parser/parser.cpp
                             ${CMAKE_CURRENT_BINARY_DIR}/parser/parser.hpp)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/parser/tokens.cpp
                       COMMAND ${WIN_FLEX_EXE} --wincompat -o ${CMAKE_CURRENT_BINARY_DIR}/parser/tokens.cpp
                               ${PROJECT_SOURCE_DIR}/src/parser/tokens.l
                       DEPENDS src/parser/tokens.l
                       VERBATIM)
    set(FLEX_Scanner_OUTPUTS ${CMAKE_CURRENT_BINARY_DIR}/parser/tokens.cpp)
else()
    find_package(BISON)
    find_package(FLEX)
    BISON_TARGET(Parser src/parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser/parser.cpp)
    FLEX_TARGET(Scanner src/parser/tokens.l  ${CMAKE_CURRENT_BINARY_DIR}/parser/tokens.cpp)
    ADD_FLEX_BISON_DEPENDENCY(Scanner Parser)
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR}/parser)
include_directories(src)

file(GLOB_RECURSE srcs src/*.cpp)
list(FILTER srcs EXCLUDE REGEX "src/main\\\\.cpp$")

# Compiler core: everything except the command-line entry point, so that
# syc itself and the test fixture can share it.
add_library(syc-core STATIC
    ${srcs}
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Scanner_OUTPUTS}
)

add_executable(syc src/main.cpp)
target_link_libraries(syc PRIVATE syc-core)

# ARMv7 assembler-level simulator, executes the assembly syc emits. It also
# provides a native implementation of the SysY runtime library (the
# counterpart of test/libsysy.a). Built as a library so syc-test can run the
# generated assembly in-process; the armsim executable is a thin wrapper.
file(GLOB_RECURSE sim_srcs armsim/*.cpp)
list(FILTER sim_srcs EXCLUDE REGEX "armsim/main\\\\.cpp$")
add_library(armsim-core STATIC ${sim_srcs})

add_executable(armsim armsim/main.cpp)
target_link_libraries(armsim PRIVATE armsim-core)

foreach(tgt syc syc-core armsim armsim-core)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /utf-8)
    endif()
endforeach()

# syc-test drives functional_test / performance_test: it compiles every .sy
# case with the syc-core compiler (in-process) and executes the generated ARM
# assembly with the in-process armsim-core interpreter, or -- with --gcc --
# via arm-linux-gnueabihf-gcc + qemu-arm. It checks stdout plus the exit code
# against the .out file.
add_executable(syc-test test/test_main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(syc-test PRIVATE syc-core armsim-core Threads::Threads)
target_include_directories(syc-test PRIVATE ${PROJECT_SOURCE_DIR}/armsim)
if(MSVC)
    target_compile_options(syc-test PRIVATE /utf-8)
endif()

foreach(mode functional performance)
    string(TOUPPER ${mode} MODE)
    if(mode STREQUAL "performance")
        set(target_name test_performance)
    else()
        set(target_name test)
    endif()
    add_custom_target(${target_name}
                      COMMAND $<TARGET_FILE:syc-test> ${mode}
                      DEPENDS syc-test
                      USES_TERMINAL
                      VERBATIM
    )
endforeach()
