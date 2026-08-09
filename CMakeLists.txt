cmake_minimum_required(VERSION 3.20)

project(dobby LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DOBBY_COMMON_SOURCES
    src/core/config.cpp
    src/core/preferences.cpp
    src/core/runtime_state.cpp
    src/diagnostics/client_schema_trace.cpp
    src/diagnostics/report_builder.cpp
    src/diagnostics/stream_probe.cpp
    src/diagnostics/violation_decoder.cpp
    src/metrics/chunk_metrics_layout.cpp
    src/metrics/client_performance.cpp
    src/metrics/network_metrics.cpp
    src/platform/files.cpp
    src/platform/process_memory.cpp
    src/platform/safe_memory.cpp
    src/platform/preferences_store.cpp
    src/ui/chest_esp.cpp
    src/ui/entity_hitbox_overlay.cpp
    src/ui/network_metrics_overlay.cpp
    src/ui/ore_esp.cpp
    src/ui/window_policy.cpp)

if(ANDROID)
    add_library(dobby SHARED
        src/dobby.cpp
        ${DOBBY_COMMON_SOURCES}
        src/hooks/chest_esp_hook.cpp
        src/hooks/chunk_metrics_hook.cpp
        src/hooks/entity_hitbox_hook.cpp
        src/hooks/minecraft_image.cpp
        src/hooks/network_metrics_hook.cpp
        src/hooks/ore_esp_scanner.cpp
        src/hooks/overlay_camera_hook.cpp
        src/hooks/packet_hooks.cpp
        src/hooks/render_camera.cpp
        src/platform/launcher.cpp
        src/platform/log.cpp
        src/ui/developer_ui.cpp)
    target_include_directories(dobby PRIVATE src)
    target_compile_options(dobby PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_link_libraries(dobby PRIVATE dl)
    target_link_libraries(dobby PRIVATE log)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(dobby PRIVATE
            -Wl,--version-script,${PROJECT_SOURCE_DIR}/exports.map
            -Wl,--no-undefined-version)
        set_target_properties(dobby PROPERTIES
            LINK_DEPENDS ${PROJECT_SOURCE_DIR}/exports.map)
    endif()
else()
    enable_testing()
    add_executable(dobby_tests tests/dobby_tests.cpp ${DOBBY_COMMON_SOURCES})
    target_include_directories(dobby_tests PRIVATE src)
    target_compile_options(dobby_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME dobby_tests COMMAND dobby_tests)
endif()
