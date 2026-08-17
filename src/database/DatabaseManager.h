#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QList>

/**
 * Represents a single row from a database query as key-value pairs.
 * Used to avoid leaking QSqlQuery to callers.
 */
struct DbRow {
    QMap<QString, QVariant> values;

    QVariant value(const QString& column) const { return values.value(column); }
    QString stringValue(const QString& column) const { return value(column).toString(); }
    qint64 int64Value(const QString& column) const { return value(column).toLongLong(); }
    int intValue(const QString& column) const { return value(column).toInt(); }
    bool boolValue(const QString& column) const { return value(column).toInt() != 0; }
};

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    bool open(const QString& databasePath);
    void close();
    bool isOpen() const;

    bool initialize();
    bool execute(const QString& sql);
    bool execute(const QString& sql, const QVariantList& args);

    QSqlQuery query(const QString& sql);
    QSqlQuery query(const QString& sql, const QVariantList& args);

    // High-level API — no QSqlQuery leakage
    QList<DbRow> queryRows(const QString& sql, const QVariantList& args = QVariantList());
    QVariant queryValue(const QString& sql, const QVariantList& args = QVariantList());

    QString lastError() const;

    bool transaction();
    bool commit();
    bool rollback();

signals:
    void databaseOpened();
    void databaseClosed();
    void errorOccurred(const QString& error);

private:
    bool createTables();
    bool createSharesTable();
    bool createTransferLogsTable();
    bool createUsersTable();
    bool createSettingsTable();

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
    QString m_lastError;
};

#endif
