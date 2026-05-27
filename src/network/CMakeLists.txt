set(BUILD_SHARED_LIBS OFF)
cmake_policy(SET CMP0077 NEW)
set(CIVETWEB_BUILD_TESTING OFF CACHE BOOL "Enable automated testing" FORCE)
set(CIVETWEB_INSTALL_EXECUTABLE OFF CACHE BOOL "Enable installing CivetWeb executable" FORCE)
set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE BOOL "Enable building of server executable" FORCE)
set(CIVETWEB_ENABLE_WEBSOCKETS ON CACHE BOOL "Enable websockets connections" FORCE)
set(CIVETWEB_ENABLE_SSL ON CACHE BOOL "Enable SSL support" FORCE)
set(CIVETWEB_ENABLE_CXX OFF)
set(CIVETWEB_ENABLE_SSL_DYNAMIC_LOADING ON CACHE BOOL "Dynamically load SSL libraries" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/civetweb_src civetweb_build)

if(MSVC)
    target_compile_options(civetweb-c-library PRIVATE /wd4244 /wd4189 /wd4267 /wd4996)
endif()

set(NETSHARE_NETWORK_SOURCES
    CivetWebServer.cpp
    mDNSService.cpp
    RequestHandler.cpp
    StreamingMultipartParser.cpp
)

set(NETSHARE_NETWORK_HEADERS
    CivetWebServer.h
    mDNSService.h
    RequestHandler.h
    StreamingMultipartParser.h
)

add_library(NetshareNetwork STATIC
    ${NETSHARE_NETWORK_SOURCES}
    ${NETSHARE_NETWORK_HEADERS}
)

set_target_properties(NetshareNetwork PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

target_include_directories(NetshareNetwork PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(NetshareNetwork PUBLIC
    Qt6::Core
    Qt6::Network
    NetshareCore
    civetweb-c-library
)

set_target_properties(NetshareNetwork PROPERTIES
    OUTPUT_NAME "NetshareNetwork"
)
