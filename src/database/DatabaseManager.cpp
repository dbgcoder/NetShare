#include "DatabaseManager.h"
#include "Logger.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include <QFile>
#include <QDir>

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent)
    , m_connectionName(QString("NetShare_Connection_%1").arg(reinterpret_cast<quintptr>(this)))
{
}

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::open(const QString& databasePath)
{
    m_databasePath = databasePath;

    QFileInfo fileInfo(databasePath);
    QDir().mkpath(fileInfo.absolutePath());

    m_database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_database.setDatabaseName(databasePath);

    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        return false;
    }

    emit databaseOpened();
    return true;
}

void DatabaseManager::close()
{
    if (m_database.isOpen()) {
        m_database.close();
        emit databaseClosed();
    }
}

bool DatabaseManager::isOpen() const
{
    return m_database.isOpen();
}

bool DatabaseManager::initialize()
{
    return createTables();
}

bool DatabaseManager::execute(const QString& sql)
{
    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::execute(const QString& sql, const QVariantList& args)
{
    QSqlQuery query(m_database);
    if (!query.prepare(sql)) {
        m_lastError = query.lastError().text();
        LOG_WARN("DatabaseManager::execute prepare failed: %s - SQL: %s",
                 qPrintable(m_lastError), qPrintable(sql.left(200)));
        return false;
    }
    for (int i = 0; i < args.size(); ++i) {
        query.bindValue(i, args[i]);
    }
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        LOG_WARN("DatabaseManager::execute failed: %s - SQL: %s",
                 qPrintable(m_lastError), qPrintable(sql.left(200)));
        return false;
    }
    return true;
}

QSqlQuery DatabaseManager::query(const QString& sql)
{
    QSqlQuery query(m_database);
    query.exec(sql);
    return query;
}

QSqlQuery DatabaseManager::query(const QString& sql, const QVariantList& args)
{
    QSqlQuery query(m_database);
    query.prepare(sql);
    for (int i = 0; i < args.size(); ++i) {
        query.bindValue(i, args[i]);
    }
    query.exec();
    return query;
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

QList<DbRow> DatabaseManager::queryRows(const QString& sql, const QVariantList& args)
{
    QList<DbRow> rows;
    QSqlQuery q(m_database);
    q.prepare(sql);
    for (int i = 0; i < args.size(); ++i) {
        q.bindValue(i, args[i]);
    }
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return rows;
    }

    while (q.next()) {
        DbRow row;
        for (int i = 0; i < q.record().count(); ++i) {
            row.values[q.record().fieldName(i)] = q.value(i);
        }
        rows.append(row);
    }
    return rows;
}

QVariant DatabaseManager::queryValue(const QString& sql, const QVariantList& args)
{
    QSqlQuery q(m_database);
    q.prepare(sql);
    for (int i = 0; i < args.size(); ++i) {
        q.bindValue(i, args[i]);
    }
    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        return QVariant();
    }
    return q.value(0);
}

bool DatabaseManager::transaction()
{
    return m_database.transaction();
}

bool DatabaseManager::commit()
{
    return m_database.commit();
}

bool DatabaseManager::rollback()
{
    return m_database.rollback();
}

bool DatabaseManager::createTables()
{
    return createSharesTable() && createTransferLogsTable() && createUsersTable() && createSettingsTable();
}

bool DatabaseManager::createSharesTable()
{
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS shares (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            token TEXT UNIQUE NOT NULL,
            file_path TEXT NOT NULL,
            file_size INTEGER DEFAULT 0,
            is_folder INTEGER DEFAULT 0,
            source INTEGER DEFAULT 0,
            expires_at TEXT,
            max_downloads INTEGER DEFAULT 0,
            download_count INTEGER DEFAULT 0,
            password_hash TEXT,
            description TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";
    if (!execute(sql)) return false;

    // Migration: add source column if it doesn't exist (for existing databases)
    execute("ALTER TABLE shares ADD COLUMN source INTEGER DEFAULT 0");
    return true;
}

bool DatabaseManager::createTransferLogsTable()
{
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS transfer_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            task_id TEXT NOT NULL,
            type TEXT NOT NULL,
            action TEXT NOT NULL,
            details TEXT,
            file_name TEXT,
            file_size INTEGER DEFAULT 0,
            transferred_size INTEGER DEFAULT 0,
            speed INTEGER DEFAULT 0,
            error TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";
    return execute(sql);
}

bool DatabaseManager::createUsersTable()
{
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            email TEXT,
            phone TEXT,
            machine_id TEXT NOT NULL,
            hardware_components TEXT,
            registration_code TEXT,
            is_trial INTEGER DEFAULT 1,
            registered_at TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";
    return execute(sql);
}

bool DatabaseManager::createSettingsTable()
{
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";
    return execute(sql);
}
