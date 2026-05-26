set(NETSHARE_NETWORK_SOURCES
    HttpServer.cpp
    mDNSService.cpp
    RequestHandler.cpp
    StreamingMultipartParser.cpp
    WebSocketHandler.cpp
)

set(NETSHARE_NETWORK_HEADERS
    HttpServer.h
    mDNSService.h
    RequestHandler.h
    StreamingMultipartParser.h
    WebSocketHandler.h
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
    Qt6::WebSockets
    NetshareCore
)

set_target_properties(NetshareNetwork PROPERTIES
    OUTPUT_NAME "NetshareNetwork"
)
