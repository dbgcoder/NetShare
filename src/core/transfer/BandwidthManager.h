#ifndef BANDWIDTHMANAGER_H
#define BANDWIDTHMANAGER_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QDateTime>
#include <QtQml/qqml.h>

class BandwidthManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit BandwidthManager(QObject* parent = nullptr);
    ~BandwidthManager() override;

    Q_INVOKABLE void setGlobalLimit(int bytesPerSecond);
    Q_INVOKABLE int globalLimit() const;

    Q_INVOKABLE void setTaskLimit(const QString& taskId, int bytesPerSecond);
    Q_INVOKABLE int taskLimit(const QString& taskId) const;
    Q_INVOKABLE void removeTaskLimit(const QString& taskId);
    Q_INVOKABLE void removeRecord(const QString& taskId);

    Q_INVOKABLE int availableBandwidth(const QString& taskId) const;
    Q_INVOKABLE void recordTransfer(const QString& taskId, int bytes);

    Q_INVOKABLE int currentSpeed(const QString& taskId) const;
    Q_INVOKABLE int globalCurrentSpeed() const;

    Q_INVOKABLE void startMonitoring();
    Q_INVOKABLE void stopMonitoring();

signals:
    void bandwidthUpdated(const QString& taskId, int speed);
    void globalBandwidthUpdated(int speed);
    void limitReached(const QString& taskId);

private:
    void onTimerTick();

    struct TransferRecord {
        qint64 bytesThisPeriod = 0;
        int speed = 0;
        int limit = 0;
    };

    int m_globalLimit;
    QMap<QString, TransferRecord> m_records;
    QTimer* m_monitorTimer;
};

#endif
