find_package(Qt6 REQUIRED COMPONENTS Test Gui Sql Network)

# QR Generator test
add_executable(netshare_test_qrcode test_qrgenerator.cpp)

set_target_properties(netshare_test_qrcode PROPERTIES
    AUTOMOC ON
)

target_include_directories(netshare_test_qrcode PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(netshare_test_qrcode PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    NetshareCore
    NetshareDatabase
    NetshareQrcode
)

# ShareManager test
add_executable(netshare_test_sharemanager
    test_sharemanager.cpp
    test_sharemanager.h
)

set_target_properties(netshare_test_sharemanager PROPERTIES
    AUTOMOC ON
)

target_include_directories(netshare_test_sharemanager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(netshare_test_sharemanager PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Test
    NetshareCore
    NetshareDatabase
)

# Core modules test (BandwidthManager, TransferLogService, ShareInfo)
add_executable(netshare_test_core
    test_core.cpp
    test_core.h
)

set_target_properties(netshare_test_core PROPERTIES
    AUTOMOC ON
)

target_include_directories(netshare_test_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(netshare_test_core PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Test
    NetshareCore
    NetshareDatabase
)

# HttpServer test
add_executable(netshare_test_httpserver
    test_httpserver.cpp
    test_httpserver.h
)

set_target_properties(netshare_test_httpserver PROPERTIES
    AUTOMOC ON
)

target_include_directories(netshare_test_httpserver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(netshare_test_httpserver PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Test
    NetshareCore
    NetshareDatabase
    NetshareNetwork
)

# Integration test
add_executable(netshare_test_integration
    test_integration.cpp
    test_integration.h
)

set_target_properties(netshare_test_integration PROPERTIES
    AUTOMOC ON
)

target_include_directories(netshare_test_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(netshare_test_integration PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    Qt6::Test
    NetshareCore
    NetshareDatabase
    NetshareNetwork
)

enable_testing()
add_test(NAME netshare_test_qrcode COMMAND netshare_test_qrcode)
add_test(NAME netshare_test_sharemanager COMMAND netshare_test_sharemanager)
add_test(NAME netshare_test_core COMMAND netshare_test_core)
add_test(NAME netshare_test_httpserver COMMAND netshare_test_httpserver)
add_test(NAME netshare_test_integration COMMAND netshare_test_integration)
