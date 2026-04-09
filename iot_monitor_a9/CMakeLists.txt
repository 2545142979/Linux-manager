cmake_minimum_required(VERSION 3.16)

project(iot_monitor_a9 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(PROJECT_SOURCES
    src/camera.c
    src/main.c
    src/protocol.c
    src/serial_port.c
    src/shared.c
    src/tcp_server.c
)

add_executable(iot_monitor_server ${PROJECT_SOURCES})

target_include_directories(iot_monitor_server PRIVATE include)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(iot_monitor_server PRIVATE -Wall -Wextra -Werror)
endif()

target_link_libraries(iot_monitor_server PRIVATE pthread)
