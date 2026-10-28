// SPDX-License-Identifier: MIT
//
// Offscreen smoke test for PdfDocumentModel. It exercises the paths that are
// otherwise only reachable through the GUI: opening a normal PDF, opening a
// static XFA form (including XFA page repagination), compositing the XFA layer
// into a QImage, and creating a flattened single-page PDF from that image.
//
// Usage: pdfly-smoke <testdata-directory> <output-directory>

#include "pdfdocumentmodel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>
#include <QtMath>

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

qint64 countNonWhitePixels(const QImage& image)
{
    qint64 count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if (qRed(line[x]) < 245 || qGreen(line[x]) < 245 || qBlue(line[x]) < 245)
            {
                ++count;
            }
        }
    }
    return count;
}

qint64 countRedPixels(const QImage& image)
{
    qint64 count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if (qRed(line[x]) > 150 && qGreen(line[x]) < 120 && qBlue(line[x]) < 120)
            {
                ++count;
            }
        }
    }
    return count;
}

QSize imageSizeAt96Dpi(const PdfDocumentModel& model, int pageIndex)
{
    const QSizeF sizeMm = model.pageSizeMm(pageIndex);
    return QSize(qMax(1, qRound(sizeMm.width() / 25.4 * 96.0)),
                 qMax(1, qRound(sizeMm.height() / 25.4 * 96.0)));
}

bool nearSize(const QSizeF& size, double widthMm, double heightMm)
{
    return qAbs(size.width() - widthMm) < 0.5 && qAbs(size.height() - heightMm) < 0.5;
}
}   // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    if (argc < 3)
    {
        out() << "Usage: pdfly-smoke <testdata-directory> <output-directory>" << Qt::endl;
        return 2;
    }

    const QDir testData(QString::fromLocal8Bit(argv[1]));
    QDir output(QString::fromLocal8Bit(argv[2]));
    if (!output.exists() && !output.mkpath(QStringLiteral(".")))
    {
        out() << "FAIL: cannot create output directory " << output.absolutePath() << Qt::endl;
        return 2;
    }

    bool ok = true;

    // ----- Normal PDF ------------------------------------------------------
    {
        PdfDocumentModel model;
        QString error;
        ok = check(model.load(testData.filePath(QStringLiteral("normal.pdf")), nullptr, &error),
                   QStringLiteral("normal.pdf opens: ") + error)
          && ok;

        if (model.isOpen())
        {
            ok = check(model.pageCount() == 1, QStringLiteral("normal.pdf has one page")) && ok;
            ok = check(!model.hasXfaForm(), QStringLiteral("normal.pdf is not XFA")) && ok;
            ok = check(!model.isEncrypted(), QStringLiteral("normal.pdf is not encrypted")) && ok;

            QStringList messages;
            const QImage image = model.renderPage(0, imageSizeAt96Dpi(model, 0), &messages);
            ok = check(!image.isNull(), QStringLiteral("normal.pdf renders")) && ok;
            if (!image.isNull())
            {
                const qint64 ink = countNonWhitePixels(image);
                ok = check(ink > 1000,
                           QStringLiteral("normal.pdf contains rendered ink (%1 pixels)").arg(ink))
                  && ok;
                ok = check(image.save(output.filePath(QStringLiteral("normal.png"))),
                           QStringLiteral("normal.png is saved")) && ok;
            }
            for (const QString& message : messages)
            {
                out() << "  render message: " << message << Qt::endl;
            }
        }
    }

    // ----- Static XFA ------------------------------------------------------
    {
        PdfDocumentModel model;
        QString error;
        ok = check(model.load(testData.filePath(QStringLiteral("static-xfa.pdf")), nullptr, &error),
                   QStringLiteral("static-xfa.pdf opens: ") + error)
          && ok;

        if (model.isOpen())
        {
            ok = check(model.hasXfaForm(), QStringLiteral("static-xfa.pdf is detected as XFA")) && ok;
            ok = check(model.pageCount() == 1, QStringLiteral("XFA form has one page")) && ok;
            ok = check(!model.isEncrypted(), QStringLiteral("static-xfa.pdf is not encrypted")) && ok;

            const QSizeF pageSize = model.pageSizeMm(0);
            ok = check(nearSize(pageSize, 215.9, 279.4),
                       QStringLiteral("XFA page was repaginated to Letter (%1 x %2 mm)")
                           .arg(pageSize.width())
                           .arg(pageSize.height()))
              && ok;

            QStringList messages;
            const QImage image = model.renderPage(0, imageSizeAt96Dpi(model, 0), &messages);
            ok = check(!image.isNull(), QStringLiteral("XFA page renders")) && ok;
            if (!image.isNull())
            {
                const qint64 red = countRedPixels(image);
                ok = check(red > 100,
                           QStringLiteral("XFA field border is composited (%1 red pixels)").arg(red))
                  && ok;
                ok = check(image.save(output.filePath(QStringLiteral("static-xfa.png"))),
                           QStringLiteral("static-xfa.png is saved")) && ok;
            }
            for (const QString& message : messages)
            {
                out() << "  render message: " << message << Qt::endl;
            }

            QByteArray flattened;
            QString flattenError;
            QStringList flattenMessages;
            ok = check(model.createFlattenedPagePdf(0, 150, &flattened, &flattenError,
                                                    &flattenMessages),
                       QStringLiteral("XFA page flattens: ") + flattenError)
              && ok;
            ok = check(!flattened.isEmpty(), QStringLiteral("flattened PDF is not empty")) && ok;

            if (!flattened.isEmpty())
            {
                QFile file(output.filePath(QStringLiteral("static-xfa-flattened.pdf")));
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(flattened);
                    file.close();
                }

                PdfDocumentModel flattenedModel;
                QString reopenedError;
                ok = check(flattenedModel.load(file.fileName(), nullptr, &reopenedError),
                           QStringLiteral("flattened PDF reopens: ") + reopenedError)
                  && ok;
                if (flattenedModel.isOpen())
                {
                    ok = check(flattenedModel.pageCount() == 1,
                               QStringLiteral("flattened PDF has one page")) && ok;
                    const QSizeF flattenedSize = flattenedModel.pageSizeMm(0);
                    ok = check(nearSize(flattenedSize, pageSize.width(), pageSize.height()),
                               QStringLiteral("flattened PDF keeps the exact page size (%1 x %2 mm)")
                                   .arg(flattenedSize.width())
                                   .arg(flattenedSize.height()))
                      && ok;

                    QStringList reopenedMessages;
                    const QImage flattenedImage = flattenedModel.renderPage(
                        0, imageSizeAt96Dpi(flattenedModel, 0), &reopenedMessages);
                    ok = check(!flattenedImage.isNull(),
                               QStringLiteral("flattened PDF renders")) && ok;
                    if (!flattenedImage.isNull())
                    {
                        const qint64 red = countRedPixels(flattenedImage);
                        ok = check(red > 100,
                                   QStringLiteral("flattened PDF still contains the XFA field (%1 red pixels)")
                                       .arg(red))
                          && ok;
                    }
                }
            }
        }
    }

    out() << (ok ? "PASS" : "FAILED") << Qt::endl;
    return ok ? 0 : 1;
}
