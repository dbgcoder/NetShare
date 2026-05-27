add_subdirectory(core)
add_subdirectory(database)
add_subdirectory(network)
add_subdirectory(qrcode)
add_subdirectory(gui)

set(APP_SOURCES main.cpp)

qt_add_executable(NetShare ${APP_SOURCES})

target_include_directories(NetShare PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/boost-di
)

target_link_libraries(NetShare PRIVATE
    NetshareCore
    NetshareDatabase
    NetshareNetwork
    NetshareQrcode
    NetshareGui
    NetshareGuiplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)

if(WIN32)
    set_target_properties(NetShare PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(NetShare PRIVATE dwmapi)
endif()
