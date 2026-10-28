// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct CupsPrinter
{
    QString name;
    QString info;
    bool isDefault = false;
};

namespace CupsBackend
{
/// Enumerates configured CUPS destinations. Returns an empty list when no
/// printers are configured or the CUPS server cannot be reached; in the latter
/// case \p errorMessage is filled in.
QVector<CupsPrinter> printers(QString* errorMessage = nullptr);
}   // namespace CupsBackend

/// A single CUPS job that can receive one or more PDF documents. Flattened
/// pages are submitted as separate documents of the same job so the job stays
/// streamable even for very large documents.
class CupsPrintJob
{
public:
    struct Options
    {
        QString printer;
        QString title;
        int copies = 1;
        bool fitToPrintableArea = true;
    };

    CupsPrintJob() = default;
    ~CupsPrintJob();

    CupsPrintJob(const CupsPrintJob&) = delete;
    CupsPrintJob& operator=(const CupsPrintJob&) = delete;

    bool begin(const Options& options, QString* errorMessage);
    bool beginDocument(const QString& documentName, bool lastDocument, QString* errorMessage);
    bool write(const QByteArray& data, QString* errorMessage);
    bool finishDocument(QString* errorMessage);
    void cancel();

    int jobId() const { return m_jobId; }
    bool isActive() const { return m_jobId > 0 && !m_jobCompleted && !m_cancelled; }

private:
    QByteArray m_printer;
    int m_jobId = 0;
    bool m_documentOpen = false;
    bool m_lastDocument = false;
    bool m_jobCompleted = false;
    bool m_cancelled = false;
};
