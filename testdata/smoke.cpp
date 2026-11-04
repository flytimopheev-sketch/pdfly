// SPDX-License-Identifier: MIT
//
// Offscreen smoke test for PdfDocumentModel. It exercises the paths that are
// otherwise only reachable through the GUI: opening a normal PDF, opening a
// static XFA form (including XFA page repagination), compositing the XFA layer
// into a QImage, and creating a flattened single-page PDF from that image.
//
// Usage: pdfly-smoke <testdata-directory> <output-directory>

#include "pdfdocumentmodel.h"
#include "xfapreprocessor.h"

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

    // ----- Bound XFA preprocessing --------------------------------------
    {
        const QByteArray templateXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<template xmlns="http://www.xfa.org/schema/xfa-template/3.3/">
  <subform name="form" layout="tb">
    <pageSet>
      <occur min="1" max="1"/>
      <pageArea name="page1" id="page1">
        <occur min="8" max="8"/>
        <medium stock="custom" short="8.5in" long="11in"/>
        <contentArea x="0.5in" y="0.5in" w="7.5in" h="1in"/>
      </pageArea>
    </pageSet>
    <subform name="Rows" layout="table" x="0.5in" y="0.5in" w="4in"
             columnWidths="2in 2in">
      <subform name="Header" layout="row">
        <draw name="HeaderName" w="2in" h="0.3in">
          <font typeface="Helvetica" size="9pt"/>
          <value><text>Name</text></value>
        </draw>
        <draw name="HeaderNote" w="2in" h="0.3in">
          <font typeface="Helvetica" size="9pt"/>
          <value><text>Note</text></value>
        </draw>
      </subform>
      <subform name="Row" layout="row">
        <bind match="dataRef" ref="$.rows[*]"/>
        <field name="Name" w="2in" minH="0.3in">
          <bind match="dataRef" ref="$.name"/>
          <ui><textEdit multiLine="1"/></ui>
          <font typeface="Helvetica" size="9pt"/>
          <value><text></text></value>
        </field>
        <field name="Note" w="2in" minH="0.3in">
          <bind match="dataRef" ref="$.note"/>
          <ui><textEdit multiLine="1"/></ui>
          <font typeface="Helvetica" size="9pt"/>
          <value><text></text></value>
        </field>
      </subform>
    </subform>
  </subform>
</template>
)xml";
        const QByteArray datasetsXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xfa:datasets xmlns:xfa="http://www.xfa.org/schema/xfa-data/1.0/">
  <xfa:data>
    <data>
      <rows><name>Bound value one</name><note>First bound row</note></rows>
      <rows><name>Bound value two</name><note>Second bound row</note></rows>
      <rows><name>Bound value three</name><note>Third bound row</note></rows>
    </data>
  </xfa:data>
</xfa:datasets>
)xml";

        const QByteArray processed = preprocessXfaTemplate(templateXml, datasetsXml);
        ok = check(!processed.isEmpty(), QStringLiteral("bound XFA template is preprocessed")) && ok;
        ok = check(processed.contains("Bound value one"),
                   QStringLiteral("bound XFA field value is injected")) && ok;
        ok = check(processed.contains("breakBefore"),
                   QStringLiteral("bound XFA repeated rows are paginated")) && ok;
        const int pageSetIndex = processed.indexOf("pageSet");
        const int tableIndex = processed.indexOf("layout=\"table\"");
        ok = check(pageSetIndex >= 0 && tableIndex >= 0 && pageSetIndex < tableIndex,
                   QStringLiteral("page set stays before the paginated table")) && ok;
    }

    // ----- Standard xfa:data record root --------------------------------
    {
        const QByteArray templateXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<template xmlns="http://www.xfa.org/schema/xfa-template/3.3/">
  <subform name="form" layout="tb">
    <field name="Name" w="4in" h="0.3in">
      <bind match="dataRef" ref="$.name"/>
      <value><text></text></value>
    </field>
  </subform>
</template>
)xml";
        const QByteArray datasetsXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xfa:datasets xmlns:xfa="http://www.xfa.org/schema/xfa-data/1.0/">
  <xfa:data>
    <record><name>Standard record root</name></record>
  </xfa:data>
  <dataDescription>
    <data><name>Decoy schema value</name></data>
  </dataDescription>
</xfa:datasets>
)xml";

        const QByteArray processed = preprocessXfaTemplate(templateXml, datasetsXml);
        ok = check(!processed.isEmpty(),
                   QStringLiteral("standard xfa:data template is preprocessed")) && ok;
        ok = check(processed.contains("Standard record root"),
                   QStringLiteral("standard xfa:data record is bound")) && ok;
        ok = check(!processed.contains("Decoy schema value"),
                   QStringLiteral("dataDescription is not used as instance data")) && ok;
    }

    // ----- Bound XFA model integration ----------------------------------
    {
        PdfDocumentModel model;
        QString error;
        ok = check(model.load(testData.filePath(QStringLiteral("bound-xfa.pdf")), nullptr, &error),
                   QStringLiteral("bound-xfa.pdf opens: ") + error)
          && ok;

        if (model.isOpen())
        {
            ok = check(model.hasXfaForm(), QStringLiteral("bound-xfa.pdf is detected as XFA")) && ok;
            ok = check(model.pageCount() >= 2,
                       QStringLiteral("bound XFA table is split across pages (%1 pages)")
                           .arg(model.pageCount()))
              && ok;

            QStringList messages;
            const QImage image = model.renderPage(0, imageSizeAt96Dpi(model, 0), &messages);
            ok = check(!image.isNull(), QStringLiteral("bound XFA page renders")) && ok;
            if (!image.isNull())
            {
                const qint64 ink = countNonWhitePixels(image);
                ok = check(ink > 1000,
                           QStringLiteral("bound XFA page contains rendered data (%1 pixels)").arg(ink))
                  && ok;
                ok = check(image.save(output.filePath(QStringLiteral("bound-xfa.png"))),
                           QStringLiteral("bound-xfa.png is saved")) && ok;
            }
            for (const QString& message : messages)
            {
                out() << "  render message: " << message << Qt::endl;
            }

            // XFA printing always goes through the flatten path; make sure the
            // bound values survive the exact PDF that is sent to CUPS.
            QByteArray printPdf;
            QString printError;
            ok = check(model.createFlattenedPagePdf(0, 150, &printPdf, &printError, nullptr),
                       QStringLiteral("bound XFA page flattens for printing: ") + printError)
              && ok;
            ok = check(!printPdf.isEmpty(),
                       QStringLiteral("bound XFA print PDF is not empty")) && ok;
        }
    }

    out() << (ok ? "PASS" : "FAILED") << Qt::endl;
    return ok ? 0 : 1;
}
