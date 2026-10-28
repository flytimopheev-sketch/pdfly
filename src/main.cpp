// SPDX-License-Identifier: MIT
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("pdfly"));
    QApplication::setOrganizationDomain(QStringLiteral("pdfly.local"));
    QApplication::setApplicationName(QStringLiteral("pdfly"));
    QApplication::setApplicationDisplayName(QStringLiteral("pdfly"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("pdfly — a lightweight read-only PDF viewer with static XFA "
                       "rendering and CUPS printing, built with Qt 6 and PDF4QT."));
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
