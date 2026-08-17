#include "BandwidthManager.h"
#include "Logger.h"

BandwidthManager::BandwidthManager(QObject* parent)
    : QObject(parent)
    , m_globalLimit(0)
    , m_monitorTimer(new QTimer(this))
{
    connect(m_monitorTimer, &QTimer::timeout, this, &BandwidthManager::onTimerTick);
}

BandwidthManager::~BandwidthManager()
{
    stopMonitoring();
}

void BandwidthManager::setGlobalLimit(int bytesPerSecond)
{
    m_globalLimit = bytesPerSecond;
    LOG_INFO("BandwidthManager: global limit set to %d bytes/s", bytesPerSecond);
}

int BandwidthManager::globalLimit() const
{
    return m_globalLimit;
}

void BandwidthManager::setTaskLimit(const QString& taskId, int bytesPerSecond)
{
    m_records[taskId].limit = bytesPerSecond;
}

int BandwidthManager::taskLimit(const QString& taskId) const
{
    return m_records.value(taskId).limit;
}

void BandwidthManager::removeTaskLimit(const QString& taskId)
{
    if (m_records.contains(taskId)) {
        m_records[taskId].limit = 0;
    }
}

void BandwidthManager::removeRecord(const QString& taskId)
{
    m_records.remove(taskId);
}

int BandwidthManager::availableBandwidth(const QString& taskId) const
{
    int taskLimitVal = m_records.value(taskId).limit;
    int available = taskLimitVal > 0 ? taskLimitVal : m_globalLimit;

    if (m_globalLimit > 0 && taskLimitVal > 0) {
        available = qMin(available, m_globalLimit);
    }

    return available;
}

void BandwidthManager::recordTransfer(const QString& taskId, int bytes)
{
    if (!m_records.contains(taskId)) {
        m_records[taskId] = TransferRecord();
    }
    m_records[taskId].bytesThisPeriod += bytes;
}

int BandwidthManager::currentSpeed(const QString& taskId) const
{
    return m_records.value(taskId).speed;
}

int BandwidthManager::globalCurrentSpeed() const
{
    int total = 0;
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        total += it.value().speed;
    }
    return total;
}

void BandwidthManager::startMonitoring()
{
    if (!m_monitorTimer->isActive()) {
        m_monitorTimer->start(500);
        LOG_INFO("BandwidthManager: monitoring started");
    }
}

void BandwidthManager::stopMonitoring()
{
    if (m_monitorTimer->isActive()) {
        m_monitorTimer->stop();
        LOG_INFO("BandwidthManager: monitoring stopped");
    }
}

void BandwidthManager::onTimerTick()
{
    int globalSpeed = 0;
    static const double alpha = 0.5;

    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        TransferRecord& record = it.value();
        qint64 rawSpeed = record.bytesThisPeriod * 2; // 折算为秒速 (500ms周期)

        if (record.speed == 0 && rawSpeed > 0) {
            // 首次有数据：直接使用原始值，快速响应
            record.speed = static_cast<int>(rawSpeed);
        } else {
            // EWMA平滑
            record.speed = static_cast<int>(alpha * rawSpeed + (1.0 - alpha) * record.speed);
        }

        record.bytesThisPeriod = 0;
        globalSpeed += record.speed;

        emit bandwidthUpdated(it.key(), record.speed);

        if (record.limit > 0 && record.speed >= record.limit) {
            emit limitReached(it.key());
        }
    }

    emit globalBandwidthUpdated(globalSpeed);

    if (m_globalLimit > 0 && globalSpeed >= m_globalLimit) {
        emit limitReached(QString());
    }
}
