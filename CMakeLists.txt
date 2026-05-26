cmake_minimum_required(VERSION 3.21)

# Generate version header from cmake/Version.cmake variables
include(cmake/Version.cmake)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common/NetShareVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/NetShareVersion.h
    @ONLY
)

project(NetShare VERSION ${NETSHARE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Sql Qml Quick QuickControls2 QuickDialogs2 Widgets WebSockets)

# Global compile options
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(web)

install(TARGETS NetShare RUNTIME DESTINATION bin)
