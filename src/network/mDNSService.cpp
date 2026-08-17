#include "mDNSService.h"
#include "Logger.h"

mDNSService::mDNSService(QObject* parent)
    : QObject(parent)
    , m_port(8080)
    , m_running(false)
{
}

mDNSService::~mDNSService()
{
    unregisterService();
}

bool mDNSService::registerService(const QString& serviceName, quint16 port)
{
    m_serviceName = serviceName;
    m_port = port;

    LOG_INFO("mDNS service registered: %s._http._tcp.local on port %d",
             qPrintable(serviceName), port);

    updateServiceTxtRecord();
    emit serviceRegistered(serviceName, port);

    m_running = true;
    return true;
}

bool mDNSService::unregisterService()
{
    if (m_running) {
        LOG_INFO("mDNS service unregistered: %s", qPrintable(m_serviceName));
        emit serviceUnregistered(m_serviceName);
        m_running = false;
        m_serviceName.clear();
    }
    return true;
}

bool mDNSService::startBrowsing()
{
    LOG_INFO("mDNS service browsing started");
    return true;
}

void mDNSService::stopBrowsing()
{
    m_discoveredServices.clear();
    LOG_INFO("mDNS service browsing stopped");
}

QMap<QString, QPair<QHostAddress, quint16>> mDNSService::discoveredServices() const
{
    return m_discoveredServices;
}

QVariantList mDNSService::getDiscoveredServicesList() const
{
    QVariantList result;
    for (auto it = m_discoveredServices.constBegin(); it != m_discoveredServices.constEnd(); ++it) {
        QVariantMap entry;
        entry["name"] = it.key();
        entry["address"] = it.value().first.toString();
        entry["port"] = it.value().second;
        result.append(entry);
    }
    return result;
}

bool mDNSService::isRunning() const
{
    return m_running;
}

QString mDNSService::serviceName() const
{
    return m_serviceName;
}

quint16 mDNSService::servicePort() const
{
    return m_port;
}

void mDNSService::updateServiceTxtRecord()
{
}
