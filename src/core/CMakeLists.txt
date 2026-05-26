set(NETSHARE_CORE_SOURCES
    common/Logger.cpp
    common/SettingsManager.cpp
)

set(NETSHARE_CORE_HEADERS
    common/IShareManager.h
    common/IFileBrowser.h
    common/IFolderPacker.h
    common/NetShareError.h
    common/ServiceLocator.h
    common/Logger.h
    common/SettingsManager.h
)

set(NETSHARE_CORE_SHARE_SOURCES
    share/ShareManager.cpp
    share/FileBrowser.cpp
    share/FolderPacker.cpp
)

set(NETSHARE_CORE_SHARE_HEADERS
    share/ShareManager.h
    share/FileBrowser.h
    share/FolderPacker.h
)

set(NETSHARE_CORE_TRANSFER_SOURCES
    transfer/FileTransferEngine.cpp
    transfer/ChunkManager.cpp
    transfer/ResumeManager.cpp
    transfer/BandwidthManager.cpp
    transfer/TransferLogService.cpp
)

set(NETSHARE_CORE_TRANSFER_HEADERS
    transfer/FileTransferEngine.h
    transfer/ChunkManager.h
    transfer/ResumeManager.h
    transfer/BandwidthManager.h
    transfer/TransferLogService.h
)

set(NETSHARE_CORE_NOTIFICATION_SOURCES
    notification/NotificationManager.cpp
)

set(NETSHARE_CORE_NOTIFICATION_HEADERS
    notification/NotificationManager.h
)

add_library(NetshareCore STATIC
    ${NETSHARE_CORE_SOURCES}
    ${NETSHARE_CORE_HEADERS}
    ${NETSHARE_CORE_SHARE_SOURCES}
    ${NETSHARE_CORE_SHARE_HEADERS}
    ${NETSHARE_CORE_TRANSFER_SOURCES}
    ${NETSHARE_CORE_TRANSFER_HEADERS}
    ${NETSHARE_CORE_NOTIFICATION_SOURCES}
    ${NETSHARE_CORE_NOTIFICATION_HEADERS}
)

set_target_properties(NetshareCore PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

target_include_directories(NetshareCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/share
    ${CMAKE_CURRENT_SOURCE_DIR}/transfer
    ${CMAKE_CURRENT_SOURCE_DIR}/notification
    ${CMAKE_CURRENT_SOURCE_DIR}/../database
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(NetshareCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Sql
    Qt6::Widgets
    Qt6::Concurrent
)

set_target_properties(NetshareCore PROPERTIES
    OUTPUT_NAME "NetshareCore"
)
