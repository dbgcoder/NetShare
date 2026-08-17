#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

class Logger : public QObject
{
    Q_OBJECT

public:
    enum Level {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
        Critical = 4
    };
    Q_ENUM(Level)

    static Logger& instance();
    static bool initialize(const QString& logDir, Level level = Info);
    static void shutdown();

    static void setLevel(Level level);
    static Level level();

    static void debug(const char* msg, ...);
    static void info(const char* msg, ...);
    static void warning(const char* msg, ...);
    static void error(const char* msg, ...);
    static void critical(const char* msg, ...);

    static void log(Level level, const char* file, int line, const char* func, const char* msg, ...);

signals:
    void logMessage(Level level, const QString& message);

private:
    Logger(QObject* parent = nullptr);
    ~Logger() override;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool openLogFile();
    void closeLogFile();
    void rotateLogFile();
    QString formatMessage(Level level, const char* file, int line, const char* func, const QString& msg);
    QString levelToString(Level level) const;

    static QString vaListToString(const char* msg, va_list args);

    QFile m_logFile;
    QTextStream m_stream;
    QMutex m_mutex;
    QString m_logDir;
    Level m_level;
    QString m_currentDate;
    qint64 m_maxFileSize;
    int m_maxFiles;
    bool m_initialized = false;
};

#define LOG_DEBUG(...) Logger::log(Logger::Debug, __FILE__, __LINE__, Q_FUNC_INFO, __VA_ARGS__)
#define LOG_INFO(...) Logger::log(Logger::Info, __FILE__, __LINE__, Q_FUNC_INFO, __VA_ARGS__)
#define LOG_WARN(...) Logger::log(Logger::Warning, __FILE__, __LINE__, Q_FUNC_INFO, __VA_ARGS__)
#define LOG_ERROR(...) Logger::log(Logger::Error, __FILE__, __LINE__, Q_FUNC_INFO, __VA_ARGS__)
#define LOG_CRITICAL(...) Logger::log(Logger::Critical, __FILE__, __LINE__, Q_FUNC_INFO, __VA_ARGS__)

#define LOG_DEBUG_SIMPLE(msg) Logger::log(Logger::Debug, __FILE__, __LINE__, Q_FUNC_INFO, "%s", msg)
#define LOG_INFO_SIMPLE(msg) Logger::log(Logger::Info, __FILE__, __LINE__, Q_FUNC_INFO, "%s", msg)
#define LOG_WARN_SIMPLE(msg) Logger::log(Logger::Warning, __FILE__, __LINE__, Q_FUNC_INFO, "%s", msg)
#define LOG_ERROR_SIMPLE(msg) Logger::log(Logger::Error, __FILE__, __LINE__, Q_FUNC_INFO, "%s", msg)

#endif
