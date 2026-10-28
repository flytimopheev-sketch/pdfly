// SPDX-License-Identifier: MIT
#include "printrunner.h"

#include "cupsprint.h"
#include "pdfdocumentmodel.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProgressDialog>
#include <QVector>
#include <QtMath>

#include <algorithm>

namespace
{
QString tr(const char* text)
{
    return QCoreApplication::translate("PrintRunner", text);
}

QVector<int> normalizedPages(const PdfDocumentModel& model, const PrintSettings& settings)
{
    QVector<int> pages = settings.pageIndices;
    if (pages.isEmpty())
    {
        pages.reserve(model.pageCount());
        for (int i = 0; i < model.pageCount(); ++i)
        {
            pages.push_back(i);
        }
    }

    pages.erase(std::remove_if(pages.begin(), pages.end(), [&model](int pageIndex)
    {
        return pageIndex < 0 || pageIndex >= model.pageCount();
    }), pages.end());

    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
}

CupsPrintJob::Options jobOptions(const PdfDocumentModel& model, const PrintSettings& settings)
{
    CupsPrintJob::Options options;
    options.printer = settings.printer;
    options.title = settings.documentTitle;
    if (options.title.isEmpty())
    {
        options.title = QFileInfo(model.filePath()).fileName();
    }
    if (options.title.isEmpty())
    {
        options.title = tr("PDF-документ");
    }
    options.copies = settings.copies;
    options.fitToPrintableArea = settings.fitToPrintableArea;
    return options;
}

PrintRunResult failure(QString* errorMessage, const QString& message)
{
    if (errorMessage)
    {
        *errorMessage = message;
    }
    return PrintRunResult::Failed;
}

PrintRunResult cancelled(QString* errorMessage)
{
    if (errorMessage)
    {
        *errorMessage = tr("Печать отменена.");
    }
    return PrintRunResult::Cancelled;
}
}   // namespace

PrintRunResult runPrintJob(PdfDocumentModel& model,
                           const PrintSettings& settings,
                           QWidget* parent,
                           QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!model.isOpen())
    {
        return failure(errorMessage, tr("PDF-документ не открыт."));
    }
    if (settings.printer.trimmed().isEmpty())
    {
        return failure(errorMessage, tr("Принтер не выбран."));
    }

    if (settings.flatten)
    {
        const QVector<int> pages = normalizedPages(model, settings);
        if (pages.isEmpty())
        {
            return failure(errorMessage, tr("Не выбрано ни одной допустимой страницы для печати."));
        }

        const int dpi = qBound(72, settings.dpi, 1200);

        CupsPrintJob job;
        QString cupsError;
        if (!job.begin(jobOptions(model, settings), &cupsError))
        {
            return failure(errorMessage, cupsError);
        }

        QProgressDialog progress(parent);
        progress.setWindowTitle(tr("Печать"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        progress.setCancelButtonText(tr("Отмена"));
        progress.setRange(0, pages.size());
        progress.setValue(0);
        progress.setLabelText(tr("Подготовка страницы 1 из %1…").arg(pages.size()));
        progress.show();
        QCoreApplication::processEvents();

        for (int i = 0; i < pages.size(); ++i)
        {
            QCoreApplication::processEvents();
            if (progress.wasCanceled())
            {
                job.cancel();
                progress.close();
                return cancelled(errorMessage);
            }

            const int pageIndex = pages.at(i);
            progress.setLabelText(tr("Отрисовка страницы %1 из %2 (страница документа %3)…")
                                      .arg(i + 1)
                                      .arg(pages.size())
                                      .arg(pageIndex + 1));

            QByteArray pagePdf;
            QString pageError;
            QStringList renderMessages;
            if (!model.createFlattenedPagePdf(pageIndex, dpi, &pagePdf, &pageError, &renderMessages))
            {
                job.cancel();
                progress.close();
                if (pageError.isEmpty())
                {
                    pageError = tr("Не удалось отобразить страницу.");
                }
                if (!renderMessages.isEmpty())
                {
                    pageError += QLatin1Char('\n') + renderMessages.mid(0, 3).join(QLatin1Char('\n'));
                }
                return failure(errorMessage,
                               tr("Не удалось подготовить страницу %1: %2").arg(pageIndex + 1).arg(pageError));
            }
            renderMessages.clear();

            const bool lastDocument = i + 1 == pages.size();
            if (!job.beginDocument(tr("Страница %1").arg(pageIndex + 1), lastDocument, &cupsError)
                || !job.write(pagePdf, &cupsError)
                || !job.finishDocument(&cupsError))
            {
                job.cancel();
                progress.close();
                return failure(errorMessage, cupsError);
            }

            progress.setValue(i + 1);
        }

        progress.close();
        return PrintRunResult::Success;
    }

    // Direct mode: the original PDF is streamed unchanged. It always contains
    // every page; the print dialog disables page selection for this mode.
    QFile file(model.filePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return failure(errorMessage,
                       tr("Не удалось открыть исходный PDF для печати: %1")
                           .arg(file.errorString()));
    }
    if (file.size() <= 0)
    {
        return failure(errorMessage, tr("Исходный PDF-файл пуст."));
    }

    CupsPrintJob job;
    QString cupsError;
    if (!job.begin(jobOptions(model, settings), &cupsError))
    {
        return failure(errorMessage, cupsError);
    }

    QProgressDialog progress(parent);
    progress.setWindowTitle(tr("Печать"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setCancelButtonText(tr("Отмена"));
    progress.setRange(0, 100);
    progress.setValue(0);
    progress.setLabelText(tr("Отправка исходного PDF в CUPS…"));
    progress.show();
    QCoreApplication::processEvents();

    const QString documentName = QFileInfo(model.filePath()).fileName();
    if (!job.beginDocument(documentName, true, &cupsError))
    {
        job.cancel();
        progress.close();
        return failure(errorMessage, cupsError);
    }

    constexpr qint64 ChunkSize = 1024 * 1024;
    const qint64 fileSize = file.size();
    while (!file.atEnd())
    {
        QCoreApplication::processEvents();
        if (progress.wasCanceled())
        {
            job.cancel();
            progress.close();
            return cancelled(errorMessage);
        }

        const QByteArray chunk = file.read(ChunkSize);
        if (chunk.isEmpty())
        {
            if (file.error() != QFileDevice::NoError)
            {
                job.cancel();
                progress.close();
                return failure(errorMessage,
                               tr("Не удалось прочитать PDF-файл: %1").arg(file.errorString()));
            }
            break;
        }

        if (!job.write(chunk, &cupsError))
        {
            job.cancel();
            progress.close();
            return failure(errorMessage, cupsError);
        }

        const int percent = static_cast<int>(qBound<qint64>(
            qint64(0), file.pos() * 100 / fileSize, qint64(100)));
        progress.setValue(percent);
    }

    if (!job.finishDocument(&cupsError))
    {
        job.cancel();
        progress.close();
        return failure(errorMessage, cupsError);
    }

    progress.setValue(100);
    progress.close();
    return PrintRunResult::Success;
}
