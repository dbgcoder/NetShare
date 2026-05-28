#ifndef DICONTAINER_H
#define DICONTAINER_H

#include <boost/di.hpp>

#include "core/common/IShareManager.h"
#include "core/common/IFileBrowser.h"
#include "core/common/IFolderPacker.h"
#include "core/share/ShareManager.h"
#include "core/share/FileBrowser.h"
#include "core/share/FolderPacker.h"
#include "core/transfer/FileTransferEngine.h"
#include "core/transfer/ChunkManager.h"
#include "core/transfer/ChunkStateManager.h"
#include "core/transfer/BandwidthManager.h"
#include "core/transfer/TransferLogService.h"
#include "core/common/SettingsManager.h"
#include "database/DatabaseManager.h"
#include "network/CivetWebServer.h"
#include "network/mDNSService.h"
#include "core/notification/NotificationManager.h"

namespace di = boost::di;

inline auto CoreModule(
    IShareManager& shareMgr,
    IFileBrowser& fileBrowser,
    IFolderPacker& folderPacker)
{
    return di::make_injector(
        di::bind<IShareManager>.to(std::ref(shareMgr)),
        di::bind<IFileBrowser>.to(std::ref(fileBrowser)),
        di::bind<IFolderPacker>.to(std::ref(folderPacker))
    );
}

inline auto TransferModule(
    FileTransferEngine& engine,
    ChunkManager& chunkMgr,
    ChunkStateManager& chunkStateMgr,
    BandwidthManager& bwMgr)
{
    return di::make_injector(
        di::bind<FileTransferEngine>.to(std::ref(engine)),
        di::bind<ChunkManager>.to(std::ref(chunkMgr)),
        di::bind<ChunkStateManager>.to(std::ref(chunkStateMgr)),
        di::bind<BandwidthManager>.to(std::ref(bwMgr))
    );
}

inline auto NetworkModule(
    CivetWebServer& server,
    mDNSService& mdns,
    NotificationManager& notif)
{
    return di::make_injector(
        di::bind<CivetWebServer>.to(std::ref(server)),
        di::bind<mDNSService>.to(std::ref(mdns)),
        di::bind<NotificationManager>.to(std::ref(notif))
    );
}

inline auto InfraModule(
    DatabaseManager& db,
    SettingsManager& settings,
    TransferLogService& logSvc)
{
    return di::make_injector(
        di::bind<DatabaseManager>.to(std::ref(db)),
        di::bind<SettingsManager>.to(std::ref(settings)),
        di::bind<TransferLogService>.to(std::ref(logSvc))
    );
}

using NetShareInjector = decltype(di::make_injector(
    std::declval<decltype(CoreModule(
        std::declval<IShareManager&>(),
        std::declval<IFileBrowser&>(),
        std::declval<IFolderPacker&>()))>(),
    std::declval<decltype(TransferModule(
        std::declval<FileTransferEngine&>(),
        std::declval<ChunkManager&>(),
        std::declval<ChunkStateManager&>(),
        std::declval<BandwidthManager&>()))>(),
    std::declval<decltype(NetworkModule(
        std::declval<CivetWebServer&>(),
        std::declval<mDNSService&>(),
        std::declval<NotificationManager&>()))>(),
    std::declval<decltype(InfraModule(
        std::declval<DatabaseManager&>(),
        std::declval<SettingsManager&>(),
        std::declval<TransferLogService&>()))>()
));

#endif
