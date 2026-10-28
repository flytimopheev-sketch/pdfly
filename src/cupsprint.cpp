// SPDX-License-Identifier: MIT
#include "cupsprint.h"

#include <QCoreApplication>

#include <algorithm>

#include <cups/cups.h>

namespace
{
QString tr(const char* text)
{
    return QCoreApplication::translate("CupsPrint", text);
}

QString lastCupsError(const QString& fallback)
{
    const char* cupsError = cupsLastErrorString();
    QString message = cupsError ? QString::fromLocal8Bit(cupsError) : QString();
    if (message.trimmed().isEmpty())
    {
        return fallback;
    }
    return tr("Ошибка CUPS: %1").arg(message);
}

QString finishError(ipp_status_t status, const QString& fallback)
{
    const char* ippError = ippErrorString(status);
    QString message = ippError ? QString::fromLocal8Bit(ippError) : QString();
    if (message.trimmed().isEmpty())
    {
        return fallback;
    }
    return tr("Ошибка CUPS: %1").arg(message);
}
}   // namespace

QVector<CupsPrinter> CupsBackend::printers(QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    cups_dest_t* destinations = nullptr;
    const int destinationCount = cupsGetDests(&destinations);

    QVector<CupsPrinter> result;
    result.reserve(qMax(0, destinationCount));

    for (int i = 0; i < destinationCount; ++i)
    {
        const cups_dest_t& destination = destinations[i];
        if (!destination.name)
        {
            continue;
        }

        CupsPrinter printer;
        printer.name = QString::fromUtf8(destination.name);
        printer.isDefault = destination.is_default != 0;

        if (const char* info = cupsGetOption("printer-info",
                                             destination.num_options,
                                             destination.options))
        {
            printer.info = QString::fromUtf8(info);
        }

        result.push_back(std::move(printer));
    }

    if (destinations)
    {
        cupsFreeDests(destinationCount, destinations);
    }

    std::sort(result.begin(), result.end(), [](const CupsPrinter& left, const CupsPrinter& right)
    {
        if (left.isDefault != right.isDefault)
        {
            return left.isDefault;
        }
        return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
    });

    if (result.isEmpty() && errorMessage)
    {
        *errorMessage = lastCupsError(
            tr("Очереди CUPS не настроены. Запустите CUPS, добавьте принтер и убедитесь, "
               "что текущий пользователь имеет к нему доступ."));
    }

    return result;
}

CupsPrintJob::~CupsPrintJob()
{
    cancel();
}

bool CupsPrintJob::begin(const Options& options, QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    cancel();

    if (options.printer.trimmed().isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = tr("Принтер не выбран.");
        }
        return false;
    }

    m_printer = options.printer.toUtf8();
    const QByteArray title = options.title.toUtf8();

    int optionCount = 0;
    cups_option_t* cupsOptions = nullptr;

    const QByteArray copies = QByteArray::number(qBound(1, options.copies, 999));
    optionCount = cupsAddOption("copies", copies.constData(), optionCount, &cupsOptions);
    if (options.fitToPrintableArea)
    {
        optionCount = cupsAddOption("print-scaling", "fit", optionCount, &cupsOptions);
    }

    m_jobId = cupsCreateJob(CUPS_HTTP_DEFAULT,
                            m_printer.constData(),
                            title.constData(),
                            optionCount,
                            cupsOptions);

    if (cupsOptions)
    {
        cupsFreeOptions(optionCount, cupsOptions);
    }

    if (m_jobId <= 0)
    {
        m_jobId = 0;
        if (errorMessage)
        {
            *errorMessage = lastCupsError(tr("Не удалось создать задание CUPS."));
        }
        return false;
    }

    m_documentOpen = false;
    m_lastDocument = false;
    m_jobCompleted = false;
    m_cancelled = false;
    return true;
}

bool CupsPrintJob::beginDocument(const QString& documentName, bool lastDocument, QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (m_jobId <= 0 || m_jobCompleted || m_cancelled)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Задание CUPS неактивно.");
        }
        return false;
    }

    if (m_documentOpen)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Документ CUPS уже открыт.");
        }
        return false;
    }

    const QByteArray name = documentName.toUtf8();
    const http_status_t status = cupsStartDocument(CUPS_HTTP_DEFAULT,
                                                   m_printer.constData(),
                                                   m_jobId,
                                                   name.constData(),
                                                   "application/pdf",
                                                   lastDocument ? 1 : 0);

    if (status != HTTP_STATUS_CONTINUE)
    {
        if (errorMessage)
        {
            *errorMessage = lastCupsError(tr("Не удалось отправить документ в CUPS."));
        }
        return false;
    }

    m_documentOpen = true;
    m_lastDocument = lastDocument;
    return true;
}

bool CupsPrintJob::write(const QByteArray& data, QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!m_documentOpen || m_cancelled)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Нет открытого документа CUPS для записи.");
        }
        return false;
    }

    constexpr qsizetype ChunkSize = 64 * 1024;
    qsizetype offset = 0;
    while (offset < data.size())
    {
        const qsizetype remaining = data.size() - offset;
        const size_t chunkSize = static_cast<size_t>(qMin(remaining, ChunkSize));

        if (cupsWriteRequestData(CUPS_HTTP_DEFAULT,
                                 data.constData() + offset,
                                 chunkSize) != HTTP_STATUS_CONTINUE)
        {
            if (errorMessage)
            {
                *errorMessage = lastCupsError(tr("Не удалось передать данные печати в CUPS."));
            }
            return false;
        }

        offset += static_cast<qsizetype>(chunkSize);
    }

    return true;
}

bool CupsPrintJob::finishDocument(QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!m_documentOpen)
    {
        if (errorMessage)
        {
            *errorMessage = tr("Нет открытого документа CUPS.");
        }
        return false;
    }

    const ipp_status_t status = cupsFinishDocument(CUPS_HTTP_DEFAULT, m_printer.constData());
    m_documentOpen = false;

    if (status < 0 || status >= IPP_STATUS_ERROR_BAD_REQUEST)
    {
        if (errorMessage)
        {
            *errorMessage = finishError(status, tr("CUPS отклонил документ."));
        }
        return false;
    }

    if (m_lastDocument)
    {
        m_jobCompleted = true;
    }

    return true;
}

void CupsPrintJob::cancel()
{
    if (m_jobId > 0 && !m_jobCompleted && !m_cancelled)
    {
        cupsCancelJob(m_printer.constData(), m_jobId);
        m_cancelled = true;
    }

    m_documentOpen = false;
}
