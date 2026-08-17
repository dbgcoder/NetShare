#ifndef MDNSSERVICE_H
#define MDNSSERVICE_H

#include <QObject>
#include <QString>
#include <QHostAddress>
#include <QMap>
#include <QVariantList>

class mDNSService : public QObject
{
    Q_OBJECT

public:
    explicit mDNSService(QObject* parent = nullptr);
    ~mDNSService() override;

    bool registerService(const QString& serviceName, quint16 port);
    bool unregisterService();

    bool startBrowsing();
    void stopBrowsing();

    QMap<QString, QPair<QHostAddress, quint16>> discoveredServices() const;

    // QML-friendly version returning QVariantList of {name, address, port}
    Q_INVOKABLE QVariantList getDiscoveredServicesList() const;

    bool isRunning() const;
    QString serviceName() const;
    quint16 servicePort() const;

signals:
    void serviceRegistered(const QString& name, quint16 port);
    void serviceUnregistered(const QString& name);
    void serviceDiscovered(const QString& name, const QHostAddress& address, quint16 port);
    void serviceLost(const QString& name);
    void errorOccurred(const QString& error);

private:
    void updateServiceTxtRecord();

private:
    QString m_serviceName;
    quint16 m_port;
    bool m_running;
    QMap<QString, QPair<QHostAddress, quint16>> m_discoveredServices;
};

#endif
