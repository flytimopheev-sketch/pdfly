// SPDX-License-Identifier: MIT
//
// CUPS integration smoke test. Requires a configured CUPS queue that accepts
// PDF (for example an ippeveprinter instance). It sends the unchanged PDF in
// direct mode and flattened XFA pages in flatten mode, exercising the same
// CupsPrintJob code used by the viewer.
//
// Usage: pdfqtviewer-print-smoke <printer-name> <testdata-directory>
//        [direct|flatten|multi|both]

#include "pdfdocumentmodel.h"
#include "printrunner.h"
#include "printsettings.h"

#include <QApplication>
#include <QDir>
#include <QTextStream>

namespace
{
QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

bool check(bool condition, const QString& message)
{
    if (!condition)
    {
        out() << "FAIL: " << message << Qt::endl;
    }
    return condition;
}
}   // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    if (argc < 3)
    {
        out() << "Usage: pdfqtviewer-print-smoke <printer-name> <testdata-directory> "
                 "[direct|flatten|multi|both]"
              << Qt::endl;
        return 2;
    }

    const QString printer = QString::fromLocal8Bit(argv[1]);
    const QDir testData(QString::fromLocal8Bit(argv[2]));
    const QString mode = argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QStringLiteral("both");
    const bool runDirect = mode == QStringLiteral("direct") || mode == QStringLiteral("both");
    const bool runFlatten = mode == QStringLiteral("flatten") || mode == QStringLiteral("both");
    const bool runMulti = mode == QStringLiteral("multi");
    bool ok = true;

    // ----- Normal PDF, direct CUPS path --------------------------------
    if (runDirect)
    {
        PdfDocumentModel model;
        QString error;
        ok = check(model.load(testData.filePath(QStringLiteral("normal.pdf")), nullptr, &error),
                   QStringLiteral("normal.pdf opens: ") + error)
          && ok;

        if (model.isOpen())
        {
            PrintSettings settings;
            settings.printer = printer;
            settings.documentTitle = QStringLiteral("PdfQtViewer direct smoke test");
            settings.copies = 1;
            settings.flatten = false;
            settings.fitToPrintableArea = true;

            QString printError;
            const PrintRunResult result = runPrintJob(model, settings, nullptr, &printError);
            ok = check(result == PrintRunResult::Success,
                       QStringLiteral("direct print succeeds: ") + printError)
              && ok;
        }
    }

    // ----- Static XFA, flatten path ------------------------------------
    if (runFlatten)
    {
        PdfDocumentModel model;
        QString error;
        ok = check(model.load(testData.filePath(QStringLiteral("static-xfa.pdf")), nullptr, &error),
                   QStringLiteral("static-xfa.pdf opens: ") + error)
          && ok;

        if (model.isOpen())
        {
            PrintSettings settings;
            settings.printer = printer;
            settings.documentTitle = QStringLiteral("PdfQtViewer XFA flatten smoke test");
            settings.copies = 1;
            settings.flatten = true;
            settings.fitToPrintableArea = true;
            settings.dpi = 150;
            for (int pageIndex = 0; pageIndex < model.pageCount(); ++pageIndex)
            {
                settings.pageIndices.push_back(pageIndex);
            }

            QString printError;
            const PrintRunResult result = runPrintJob(model, settings, nullptr, &printError);
            ok = check(result == PrintRunResult::Success,
                       QStringLiteral("XFA flatten print succeeds: ") + printError)
              && ok;
        }
    }

    // ----- Two-page flatten, multiple CUPS documents in one job ---------
    if (runMulti)
    {
        PdfDocumentModel model;
        QString error;
        ok = check(model.load(testData.filePath(QStringLiteral("normal-2page.pdf")), nullptr, &error),
                   QStringLiteral("normal-2page.pdf opens: ") + error)
          && ok;

        if (model.isOpen())
        {
            ok = check(model.pageCount() == 2,
                       QStringLiteral("normal-2page.pdf has two pages")) && ok;

            PrintSettings settings;
            settings.printer = printer;
            settings.documentTitle = QStringLiteral("PdfQtViewer multi-page flatten smoke test");
            settings.copies = 1;
            settings.flatten = true;
            settings.fitToPrintableArea = true;
            settings.dpi = 100;
            settings.pageIndices = {0, 1};

            QString printError;
            const PrintRunResult result = runPrintJob(model, settings, nullptr, &printError);
            ok = check(result == PrintRunResult::Success,
                       QStringLiteral("two-page flatten print succeeds: ") + printError)
              && ok;
        }
    }

    out() << (ok ? "PASS" : "FAILED") << Qt::endl;
    return ok ? 0 : 1;
}
