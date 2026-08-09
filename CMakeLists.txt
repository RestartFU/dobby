cmake_minimum_required(VERSION 3.20)

project(packet_debugger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
    add_library(packet_debugger SHARED src/packet_debugger.cpp)
    target_include_directories(packet_debugger PRIVATE src)
    target_compile_options(packet_debugger PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_link_libraries(packet_debugger PRIVATE dl)
    target_link_libraries(packet_debugger PRIVATE log)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(packet_debugger PRIVATE
            -Wl,--version-script,${PROJECT_SOURCE_DIR}/exports.map
            -Wl,--no-undefined-version)
        set_target_properties(packet_debugger PROPERTIES
            LINK_DEPENDS ${PROJECT_SOURCE_DIR}/exports.map)
    endif()
else()
    enable_testing()
    add_executable(layout_test tests/layout_test.cpp)
    target_include_directories(layout_test PRIVATE src)
    target_compile_options(layout_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME layout_test COMMAND layout_test)
endif()
