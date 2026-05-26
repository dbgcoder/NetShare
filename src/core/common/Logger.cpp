#include "Logger.h"

#include <cstdarg>
#include <cstdio>

Logger::Logger(QObject* parent)
    : QObject(parent)
    , m_level(Info)
    , m_maxFileSize(10 * 1024 * 1024)
    , m_maxFiles(5)
    , m_initialized(false)
{
}

Logger::~Logger()
{
    closeLogFile();
}

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

bool Logger::initialize(const QString& logDir, Level level)
{
    Logger& inst = instance();
    QMutexLocker locker(&inst.m_mutex);

    // Close previous log file if re-initializing
    inst.closeLogFile();

    inst.m_logDir = logDir;
    inst.m_level = level;

    if (!QDir().mkpath(logDir)) {
        qWarning() << "Failed to create log directory:" << logDir;
        return false;
    }

    if (!inst.openLogFile()) {
        return false;
    }

    inst.m_initialized = true;
    return true;
}

void Logger::shutdown()
{
    Logger& inst = instance();
    QMutexLocker locker(&inst.m_mutex);
    inst.closeLogFile();
    inst.m_initialized = false;
}

void Logger::setLevel(Level level)
{
    Logger& inst = instance();
    inst.m_level = level;
}

Logger::Level Logger::level()
{
    return instance().m_level;
}

void Logger::debug(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    QString message = vaListToString(msg, args);
    va_end(args);
    log(Debug, nullptr, 0, nullptr, "%s", qPrintable(message));
}

void Logger::info(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    QString message = vaListToString(msg, args);
    va_end(args);
    log(Info, nullptr, 0, nullptr, "%s", qPrintable(message));
}

void Logger::warning(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    QString message = vaListToString(msg, args);
    va_end(args);
    log(Warning, nullptr, 0, nullptr, "%s", qPrintable(message));
}

void Logger::error(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    QString message = vaListToString(msg, args);
    va_end(args);
    log(Error, nullptr, 0, nullptr, "%s", qPrintable(message));
}

void Logger::critical(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    QString message = vaListToString(msg, args);
    va_end(args);
    log(Critical, nullptr, 0, nullptr, "%s", qPrintable(message));
}

void Logger::log(Level level, const char* file, int line, const char* func, const char* msg, ...)
{
    Logger& inst = instance();

    if (!inst.m_initialized) {
        return;
    }

    if (level < inst.m_level) {
        return;
    }

    va_list args;
    va_start(args, msg);
    QString message = vaListToString(msg, args);
    va_end(args);

    QString formatted = inst.formatMessage(level, file, line, func, message);

    QMutexLocker locker(&inst.m_mutex);

    QString currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    if (currentDate != inst.m_currentDate) {
        inst.closeLogFile();
        inst.openLogFile();
    }

    if (inst.m_logFile.isOpen()) {
        inst.m_stream << formatted << "\n";
        inst.m_stream.flush();
        inst.m_logFile.flush();
    }

    QFileInfo fileInfo(inst.m_logFile);
    if (fileInfo.size() >= inst.m_maxFileSize) {
        inst.rotateLogFile();
    }

    emit inst.logMessage(level, formatted);

    fprintf(stderr, "%s\n", qPrintable(formatted));
}

bool Logger::openLogFile()
{
    m_currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString fileName = QString("netshare_%1.log").arg(m_currentDate);
    QString filePath = m_logDir + "/" + fileName;

    m_logFile.setFileName(filePath);

    QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text;
    if (!m_logFile.open(mode)) {
        qWarning() << "Failed to open log file:" << filePath;
        return false;
    }

    m_stream.setDevice(&m_logFile);
    return true;
}

void Logger::closeLogFile()
{
    if (m_stream.device()) {
        m_stream.flush();
    }
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void Logger::rotateLogFile()
{
    closeLogFile();

    for (int i = m_maxFiles - 1; i > 0; --i) {
        QString oldFile = QString("%1/netshare_%2_%3.log")
                              .arg(m_logDir)
                              .arg(m_currentDate)
                              .arg(i);
        QString newFile = QString("%1/netshare_%2_%3.log")
                              .arg(m_logDir)
                              .arg(m_currentDate)
                              .arg(i + 1);

        QFile f(oldFile);
        if (f.exists()) {
            f.rename(newFile);
        }
    }

    QString firstFile = QString("%1/netshare_%2_1.log").arg(m_logDir).arg(m_currentDate);
    if (QFile::exists(firstFile)) {
        QFile::remove(firstFile);
    }

    if (m_logFile.exists()) {
        m_logFile.rename(firstFile);
    }

    openLogFile();
}

QString Logger::formatMessage(Level level, const char* file, int line, const char* func, const QString& msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString levelStr = levelToString(level);

    if (file && line > 0 && func) {
        QFileInfo fileInfo(file);
        QString fileName = fileInfo.fileName();
        return QString("[%1] [%2] [%3:%4] %5")
            .arg(timestamp, levelStr, fileName)
            .arg(line)
            .arg(msg);
    } else {
        return QString("[%1] [%2] %3")
            .arg(timestamp, levelStr, msg);
    }
}

QString Logger::levelToString(Level level) const
{
    switch (level) {
        case Debug:    return "DEBUG";
        case Info:     return "INFO ";
        case Warning:  return "WARN ";
        case Error:    return "ERROR";
        case Critical: return "CRIT ";
        default:       return "UNKN ";
    }
}

QString Logger::vaListToString(const char* msg, va_list args)
{
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    return QString::fromUtf8(buffer);
}
