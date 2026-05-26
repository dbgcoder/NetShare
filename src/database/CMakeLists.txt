set(NETSHARE_DATABASE_SOURCES
    DatabaseManager.cpp
)

set(NETSHARE_DATABASE_HEADERS
    DatabaseManager.h
)

add_library(NetshareDatabase STATIC
    ${NETSHARE_DATABASE_SOURCES}
    ${NETSHARE_DATABASE_HEADERS}
)

set_target_properties(NetshareDatabase PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

target_include_directories(NetshareDatabase PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(NetshareDatabase PUBLIC
    Qt6::Core
    Qt6::Sql
    NetshareCore
)

set_target_properties(NetshareDatabase PROPERTIES
    OUTPUT_NAME "NetshareDatabase"
)
