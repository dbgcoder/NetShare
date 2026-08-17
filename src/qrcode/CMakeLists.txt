set(NETSHARE_QRCODE_SOURCES
    qrcodegen.cpp
)

set(NETSHARE_QRCODE_HEADERS
    qrcodegen.hpp
)

add_library(NetshareQrcode STATIC
    ${NETSHARE_QRCODE_SOURCES}
    ${NETSHARE_QRCODE_HEADERS}
)

set_target_properties(NetshareQrcode PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# Suppress warnings in third-party qrcodegen library
if(MSVC)
    set_source_files_properties(qrcodegen.cpp PROPERTIES COMPILE_FLAGS /wd4244)
else()
    set_source_files_properties(qrcodegen.cpp PROPERTIES COMPILE_FLAGS -Wno-conversion)
endif()

target_include_directories(NetshareQrcode PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(NetshareQrcode PUBLIC
    Qt6::Core
    Qt6::Gui
    NetshareCore
)

set_target_properties(NetshareQrcode PROPERTIES
    OUTPUT_NAME "NetshareQrcode"
)
