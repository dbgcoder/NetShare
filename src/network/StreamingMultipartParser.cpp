#include "StreamingMultipartParser.h"
#include "Logger.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

StreamingMultipartParser::StreamingMultipartParser(const QString& saveDir)
    : m_saveDir(saveDir)
    , m_bufferOffset(0)
{
    QDir().mkpath(m_saveDir);
}

StreamingMultipartParser::~StreamingMultipartParser()
{
    closeCurrentFile();
}

bool StreamingMultipartParser::init(const QString& contentType)
{
    int boundaryPos = contentType.indexOf("boundary=");
    if (boundaryPos < 0) return false;

    QString boundary = contentType.mid(boundaryPos + 9);
    int semiPos = boundary.indexOf(';');
    if (semiPos >= 0) boundary = boundary.left(semiPos);
    if (boundary.startsWith('"') && boundary.endsWith('"'))
        boundary = boundary.mid(1, boundary.size() - 2);
    boundary = boundary.trimmed();

    m_boundaryBytes = "--" + boundary.toUtf8();
    m_endBoundary = m_boundaryBytes + "--";

    // Overlap size: enough to detect a boundary that straddles two chunks.
    // Maximum: \r\n + boundary + \r\n or \r\n + endBoundary
    m_overlapSize = m_boundaryBytes.size() + 8;

    LOG_INFO("StreamingMultipartParser initialized: boundary=%s, saveDir=%s",
             qPrintable(QString::fromUtf8(m_boundaryBytes)), qPrintable(m_saveDir));
    return true;
}

void StreamingMultipartParser::feed(const QByteArray& data)
{
    m_buffer.append(data);
    processBuffer();
}

QList<StreamingMultipartParser::SavedFile> StreamingMultipartParser::finish()
{
    // Process any remaining data
    processBuffer();

    // If we're still streaming a file, close it (incomplete upload)
    if (m_state == ParseState::StreamFileData && m_currentFile) {
        saveCurrentFile();
        closeCurrentFile();
        m_state = ParseState::FindBoundary;
    }

    return m_savedFiles;
}

void StreamingMultipartParser::processBuffer()
{
    // Keep processing as long as we can make progress
    int lastOffset = -1;
    int iterations = 0;
    while (iterations < 100 && m_bufferOffset != lastOffset) {
        iterations++;
        lastOffset = m_bufferOffset;

        switch (m_state) {
        case ParseState::FindBoundary:
            handleFindBoundary();
            break;
        case ParseState::ParseHeaders:
            handleParseHeaders();
            break;
        case ParseState::StreamFileData:
            handleStreamFileData();
            break;
        case ParseState::BufferFormData:
            handleBufferFormData();
            break;
        }

        // If state didn't change and offset didn't change, no progress
        if (m_bufferOffset == lastOffset) break;
    }

    // Compact buffer: remove already-processed data from the front
    if (m_bufferOffset > 0) {
        if (m_bufferOffset >= m_buffer.size()) {
            m_buffer.clear();
        } else {
            m_buffer = m_buffer.mid(m_bufferOffset);
        }
        m_bufferOffset = 0;
    }
}

void StreamingMultipartParser::handleFindBoundary()
{
    // Search for the next boundary starting from unprocessed data
    int searchStart = m_bufferOffset;
    int boundaryPos = m_buffer.indexOf(m_boundaryBytes, searchStart);
    if (boundaryPos < 0) {
        // No boundary found. Keep overlap bytes at the end in case boundary
        // straddles the next chunk, discard the rest.
        int keepFrom = m_buffer.size() - m_overlapSize;
        if (keepFrom > m_bufferOffset) {
            m_bufferOffset = keepFrom;
        }
        return; // No progress possible
    }

    // Check for end boundary
    if (m_buffer.mid(boundaryPos, m_endBoundary.size()) == m_endBoundary) {
        // End of multipart body
        m_bufferOffset = m_buffer.size(); // Consume everything
        m_state = ParseState::FindBoundary;
        return;
    }

    // Skip past the boundary line and any \r\n after it
    int afterBoundary = boundaryPos + m_boundaryBytes.size();
    while (afterBoundary < m_buffer.size() &&
           (m_buffer[afterBoundary] == '\r' || m_buffer[afterBoundary] == '\n'))
        afterBoundary++;

    m_bufferOffset = afterBoundary;
    m_state = ParseState::ParseHeaders;
}

void StreamingMultipartParser::handleParseHeaders()
{
    // Look for the end of headers (\r\n\r\n) in unprocessed buffer
    int headerEnd = m_buffer.indexOf("\r\n\r\n", m_bufferOffset);
    if (headerEnd < 0) {
        // Headers not complete yet - keep data and wait for more
        return;
    }

    // Extract and parse headers (using mid only for the small header area)
    QByteArray partHeaders = m_buffer.mid(m_bufferOffset, headerEnd - m_bufferOffset);
    QString headersStr = QString::fromUtf8(partHeaders);

    QString fileName;
    QRegularExpression nameRe("filename=\"([^\"]+)\"");
    QRegularExpressionMatch match = nameRe.match(headersStr);
    if (match.hasMatch()) {
        fileName = match.captured(1);
    }

    int dataStart = headerEnd + 4; // After \r\n\r\n
    m_bufferOffset = dataStart;

    if (fileName.isEmpty()) {
        // Non-file form field
        m_state = ParseState::BufferFormData;
        m_currentFileName.clear();
        return;
    }

    // It's a file part - prepare to stream data to disk
    QString relativePath = fileName;
    relativePath.replace('\\', '/');

    QString savePath;
    bool isResume = false;

    if (relativePath.contains('/')) {
        savePath = m_saveDir + "/" + relativePath;
        QDir().mkpath(QFileInfo(savePath).absolutePath());
        m_isFolderUpload = true;
        m_folderRoot = relativePath.section('/', 0, 0);
    } else {
        savePath = m_saveDir + "/" + relativePath;
    }

    // Check if this is a resume upload (partial file exists with matching path)
    if (m_resumeOffset > 0 && !m_resumeFilePath.isEmpty()) {
        QFileInfo resumeFi(m_resumeFilePath);
        if (resumeFi.exists() && resumeFi.isFile() && resumeFi.size() == m_resumeOffset) {
            savePath = m_resumeFilePath;
            isResume = true;
            LOG_INFO("StreamingMultipartParser: resuming file at offset %lld: %s",
                     m_resumeOffset, qPrintable(savePath));
        } else {
            LOG_WARN("StreamingMultipartParser: resume file validation failed (expected %lld bytes, got %lld), falling back to new upload",
                     m_resumeOffset, resumeFi.exists() ? resumeFi.size() : 0);
        }
    }

    // Conflict resolution: only for non-resume flat file uploads
    if (!isResume && !relativePath.contains('/') && QFile::exists(savePath)) {
        QString baseName = QFileInfo(relativePath).completeBaseName();
        QString ext = QFileInfo(relativePath).suffix();
        int counter = 1;
        do {
            if (ext.isEmpty()) {
                savePath = m_saveDir + "/" + baseName + QString(" (%1)").arg(counter);
            } else {
                savePath = m_saveDir + "/" + baseName + QString(" (%1).").arg(counter) + ext;
            }
            counter++;
        } while (QFile::exists(savePath));
    }

    m_currentFileName = relativePath;
    m_currentSavePath = savePath;
    m_currentFileBytesWritten = isResume ? m_resumeOffset : 0;

    // Open file for writing (append mode for resume, normal mode for new files)
    m_currentFile = new QFile(savePath);
    QIODevice::OpenMode openMode = isResume
        ? (QIODevice::WriteOnly | QIODevice::Append)
        : QIODevice::WriteOnly;
    if (!m_currentFile->open(openMode)) {
        LOG_ERROR("Failed to open file for streaming write: %s", qPrintable(savePath));
        delete m_currentFile;
        m_currentFile = nullptr;
        m_state = ParseState::FindBoundary;
        return;
    }

    LOG_INFO("StreamingMultipartParser: streaming to %s%s", qPrintable(savePath),
             isResume ? " (resume mode)" : "");
    m_state = ParseState::StreamFileData;
}

void StreamingMultipartParser::handleStreamFileData()
{
    // We have unprocessed data from m_bufferOffset onwards.
    // We need to write as much as possible to the file, but must keep
    // overlap bytes at the end to detect boundary that may straddle chunks.

    int availableData = m_buffer.size() - m_bufferOffset;
    if (availableData <= 0) return;

    // Search for boundary in the unprocessed data
    int boundaryPos = m_buffer.indexOf(m_boundaryBytes, m_bufferOffset);

    if (boundaryPos >= 0) {
        // Found boundary - write data up to (boundary - \r\n) to file
        int dataEnd = boundaryPos;
        // Remove the \r\n that precedes the boundary
        if (dataEnd > m_bufferOffset && m_buffer[dataEnd - 1] == '\n') dataEnd--;
        if (dataEnd > m_bufferOffset && m_buffer[dataEnd - 1] == '\r') dataEnd--;

        int writeSize = dataEnd - m_bufferOffset;
        if (writeSize > 0 && m_currentFile) {
            m_currentFile->write(m_buffer.constData() + m_bufferOffset, writeSize);
            m_currentFileBytesWritten += writeSize;
            if (m_progressCallback) {
                m_progressCallback(m_currentFileBytesWritten);
            }
        }

        // Save and close the file
        saveCurrentFile();
        closeCurrentFile();

        m_bufferOffset = boundaryPos; // Position at boundary for FindBoundary
        m_state = ParseState::FindBoundary;
        return;
    }

    // No boundary found. Write data to file, but keep overlap bytes
    // at the end for boundary detection across chunk boundaries.
    int safeWriteEnd = m_buffer.size() - m_overlapSize;
    int writeSize = safeWriteEnd - m_bufferOffset;

    if (writeSize > 0 && m_currentFile) {
        m_currentFile->write(m_buffer.constData() + m_bufferOffset, writeSize);
        m_currentFileBytesWritten += writeSize;
        m_bufferOffset = safeWriteEnd;
        if (m_progressCallback) {
            m_progressCallback(m_currentFileBytesWritten);
        }
    }
    // Wait for more data (or the boundary to arrive)
}

void StreamingMultipartParser::handleBufferFormData()
{
    // Find the next boundary to determine the end of the non-file field
    int boundaryPos = m_buffer.indexOf(m_boundaryBytes, m_bufferOffset);
    if (boundaryPos < 0) {
        // Not found yet, keep overlap bytes
        int keepFrom = m_buffer.size() - m_overlapSize;
        if (keepFrom > m_bufferOffset) {
            m_bufferOffset = keepFrom;
        }
        return;
    }

    // Skip past this form field
    m_bufferOffset = boundaryPos;
    m_state = ParseState::FindBoundary;
}

void StreamingMultipartParser::closeCurrentFile()
{
    if (m_currentFile) {
        m_currentFile->close();
        delete m_currentFile;
        m_currentFile = nullptr;
    }
}

void StreamingMultipartParser::saveCurrentFile()
{
    if (m_currentFileBytesWritten > 0 || m_currentFile) {
        SavedFile sf;
        sf.fileName = m_currentFileName;
        sf.savePath = m_currentSavePath;
        sf.fileSize = m_currentFileBytesWritten;
        m_savedFiles.append(sf);
        m_totalSize += m_currentFileBytesWritten;

        LOG_INFO("Streaming saved: %s (%lld bytes)", qPrintable(m_currentSavePath), m_currentFileBytesWritten);
    }
}
