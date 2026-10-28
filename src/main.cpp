// SPDX-License-Identifier: MIT
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PdfQtViewer"));
    QApplication::setOrganizationDomain(QStringLiteral("pdfqtviewer.local"));
    QApplication::setApplicationName(QStringLiteral("pdfqtviewer"));
    QApplication::setApplicationDisplayName(QStringLiteral("PDF Qt Viewer"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A lightweight read-only Qt/PDF4QT PDF viewer with static XFA "
                       "rendering and CUPS printing."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("PDF document to open."),
                                 QStringLiteral("[file]"));
    parser.process(application);

    MainWindow window;
    window.show();

    const QStringList positionalArguments = parser.positionalArguments();
    if (!positionalArguments.isEmpty())
    {
        const QString filePath = positionalArguments.first();
        QTimer::singleShot(0, &window, [&window, filePath]
        {
            window.openFile(filePath);
        });
    }

    return application.exec();
}
