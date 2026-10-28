// SPDX-License-Identifier: MIT
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("pdfly"));
    QApplication::setOrganizationDomain(QStringLiteral("pdfly.local"));
    QApplication::setApplicationName(QStringLiteral("pdfly"));
    QApplication::setApplicationDisplayName(QStringLiteral("pdfly"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QApplication::setDesktopFileName(QStringLiteral("pdfly"));

    const QLocale russianLocale(QLocale::Russian, QLocale::Russia);
    QLocale::setDefault(russianLocale);

    // The application strings are already Russian. Load Qt's own Russian
    // translations as well, so standard dialogs and buttons follow the UI.
    QTranslator qtTranslator;
    if (qtTranslator.load(russianLocale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    {
        application.installTranslator(&qtTranslator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("pdfly — лёгкий просмотрщик PDF с поддержкой статических "
                       "XFA-форм и печатью через CUPS. Создан на Qt 6 и PDF4QT."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("файл"),
                                 QStringLiteral("PDF-документ для открытия."),
                                 QStringLiteral("[файл]"));
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
