#ifndef NETSHAREERROR_H
#define NETSHAREERROR_H

#include <QString>
#include <QDebug>

/**
 * Unified error code enumeration for NetShare.
 * All public APIs should return ErrorCode or Result<T> instead of bool/empty values.
 */
enum class ErrorCode {
    // General
    NoError = 0,
    UnknownError,

    // Database (100-199)
    DatabaseNotOpen = 100,
    DatabaseQueryFailed,
    DatabaseTransactionFailed,

    // Share (200-299)
    ShareNotFound = 200,
    ShareExpired,
    SharePasswordInvalid,
    ShareMaxDownloadsReached,
    ShareTokenInvalid,
    ShareFilePathInvalid,

    // Transfer (300-399)
    TransferNotFound = 300,
    TransferAlreadyExists,
    TransferChunkFailed,
    TransferMergeFailed,
    TransferNetworkError,
    TransferFileOpenFailed,
    TransferPaused,
    TransferCancelled,

    // Network (400-499)
    ServerStartFailed = 400,
    ServerAlreadyRunning,
    ServerNotRunning,
    WebSocketStartFailed,
    MdnsRegistrationFailed,

    // File (500-599)
    FileNotFound = 500,
    FileReadFailed,
    FileWriteFailed,
    FolderPackFailed,
    FolderUnpackFailed,
    DirectoryNotFound,

    // Settings (600-699)
    SettingsLoadFailed = 600,
    SettingsSaveFailed,
    SettingsInvalidFormat,
};

/**
 * Convert ErrorCode to human-readable string.
 */
inline QString errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::NoError:              return "No error";
        case ErrorCode::UnknownError:         return "Unknown error";

        case ErrorCode::DatabaseNotOpen:       return "Database not open";
        case ErrorCode::DatabaseQueryFailed:   return "Database query failed";
        case ErrorCode::DatabaseTransactionFailed: return "Database transaction failed";

        case ErrorCode::ShareNotFound:         return "Share not found";
        case ErrorCode::ShareExpired:          return "Share expired";
        case ErrorCode::SharePasswordInvalid:  return "Invalid share password";
        case ErrorCode::ShareMaxDownloadsReached: return "Max downloads reached";
        case ErrorCode::ShareTokenInvalid:     return "Invalid share token";
        case ErrorCode::ShareFilePathInvalid:  return "Invalid file path";

        case ErrorCode::TransferNotFound:      return "Transfer not found";
        case ErrorCode::TransferAlreadyExists: return "Transfer already exists";
        case ErrorCode::TransferChunkFailed:   return "Chunk download failed";
        case ErrorCode::TransferMergeFailed:   return "Chunk merge failed";
        case ErrorCode::TransferNetworkError:  return "Network error during transfer";
        case ErrorCode::TransferFileOpenFailed: return "Failed to open file for transfer";
        case ErrorCode::TransferPaused:        return "Transfer paused";
        case ErrorCode::TransferCancelled:     return "Transfer cancelled";

        case ErrorCode::ServerStartFailed:     return "Server start failed";
        case ErrorCode::ServerAlreadyRunning:  return "Server already running";
        case ErrorCode::ServerNotRunning:      return "Server not running";
        case ErrorCode::WebSocketStartFailed:  return "WebSocket start failed";
        case ErrorCode::MdnsRegistrationFailed: return "mDNS registration failed";

        case ErrorCode::FileNotFound:          return "File not found";
        case ErrorCode::FileReadFailed:        return "File read failed";
        case ErrorCode::FileWriteFailed:       return "File write failed";
        case ErrorCode::FolderPackFailed:      return "Folder pack failed";
        case ErrorCode::FolderUnpackFailed:    return "Folder unpack failed";
        case ErrorCode::DirectoryNotFound:     return "Directory not found";

        case ErrorCode::SettingsLoadFailed:    return "Settings load failed";
        case ErrorCode::SettingsSaveFailed:    return "Settings save failed";
        case ErrorCode::SettingsInvalidFormat: return "Settings invalid format";

        default: return "Unmapped error code";
    }
}

/**
 * Debug output for ErrorCode.
 */
inline QDebug operator<<(QDebug debug, ErrorCode code) {
    QDebugStateSaver saver(debug);
    debug.nospace() << "ErrorCode(" << static_cast<int>(code) << ", " << errorCodeToString(code) << ")";
    return debug;
}

/**
 * Result type for operations that can fail with a value.
 * Usage:
 *   Result<QString> token = shareManager->createShare(...);
 *   if (token) { use token.value; }
 *   else { handle token.error; }
 */
template<typename T>
class Result {
public:
    // Success constructor
    Result(T value) : m_value(std::move(value)), m_error(ErrorCode::NoError), m_isSuccess(true) {}

    // Error constructor
    Result(ErrorCode error, QString message = QString())
        : m_error(error), m_errorMessage(std::move(message)), m_isSuccess(false) {}

    // Bool conversion for error checking
    explicit operator bool() const { return m_isSuccess; }

    bool isSuccess() const { return m_isSuccess; }
    bool isError() const { return !m_isSuccess; }

    // Value access (undefined behavior if error)
    const T& value() const { return m_value; }
    T& value() { return m_value; }

    // Error access
    ErrorCode error() const { return m_error; }
    QString errorMessage() const { return m_errorMessage.isEmpty() ? errorCodeToString(m_error) : m_errorMessage; }

    // Convenience: get value or default
    T valueOrDefault(const T& defaultValue = T()) const {
        return m_isSuccess ? m_value : defaultValue;
    }

private:
    T m_value;
    ErrorCode m_error = ErrorCode::NoError;
    QString m_errorMessage;
    bool m_isSuccess;
};

/**
 * Specialization for void results (operations that only succeed/fail).
 */
template<>
class Result<void> {
public:
    // Success
    Result() : m_error(ErrorCode::NoError), m_isSuccess(true) {}

    // Error
    Result(ErrorCode error, QString message = QString())
        : m_error(error), m_errorMessage(std::move(message)), m_isSuccess(false) {}

    explicit operator bool() const { return m_isSuccess; }
    bool isSuccess() const { return m_isSuccess; }
    bool isError() const { return !m_isSuccess; }

    ErrorCode error() const { return m_error; }
    QString errorMessage() const { return m_errorMessage.isEmpty() ? errorCodeToString(m_error) : m_errorMessage; }

    // Factory methods
    static Result<void> ok() { return Result<void>(); }
    static Result<void> fail(ErrorCode code, const QString& msg = QString()) { return Result<void>(code, msg); }

private:
    ErrorCode m_error;
    QString m_errorMessage;
    bool m_isSuccess;
};

#endif // NETSHAREERROR_H
