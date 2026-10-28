// SPDX-License-Identifier: MIT
#include "mainwindow.h"

#include "pageview.h"
#include "pdfdocumentmodel.h"
#include "printdialog.h"
#include "printrunner.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QUrl>
#include <QtMath>

namespace
{
QIcon themedIcon(const QString& name,
                 QStyle::StandardPixmap fallback = QStyle::SP_CustomBase)
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull() && fallback != QStyle::SP_CustomBase)
    {
        icon = QApplication::style()->standardIcon(fallback);
    }
    return icon;
}
}   // namespace

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent)
{
    setAcceptDrops(true);
    resize(1200, 820);
    setWindowTitle(tr("pdfly"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("pdfly"),
                                   QIcon::fromTheme(QStringLiteral("application-pdf"))));

    m_model = new PdfDocumentModel(this);
    m_pageView = new PageView(this);
    setCentralWidget(m_pageView);
    m_pageView->setModel(m_model);

    createActions();
    createMenusAndToolBar();

    m_pageStatusLabel = new QLabel(tr("Документ не открыт"), this);
    m_xfaLabel = new QLabel(tr("XFA · только чтение"), this);
    m_xfaLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: #fff7ed; color: #9a3412; border: 1px solid #fdba74;"
        " border-radius: 8px; padding: 1px 8px; font-weight: 600; }"));
    m_xfaLabel->setVisible(false);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addPermanentWidget(m_xfaLabel);
    statusBar()->addPermanentWidget(m_pageStatusLabel);

    connect(m_model, &PdfDocumentModel::documentChanged,
            this, &MainWindow::updateDocumentUi);
    connect(m_model, &PdfDocumentModel::errorOccurred,
            this, [this](const QString& message)
    {
        statusBar()->showMessage(message, 8000);
    });
    connect(m_pageView, &PageView::pageChanged,
            this, &MainWindow::updatePageUi);
    connect(m_pageView, &PageView::zoomChanged,
            this, &MainWindow::updateZoomUi);
    connect(m_pageView, &PageView::rotationChanged,
            this, &MainWindow::updateRotationUi);
    connect(m_pageView, &PageView::renderFailed,
            this, &MainWindow::showRenderFailure);

    updateDocumentUi();
    updateZoomUi(m_pageView->zoomFactor());
    updateRotationUi(0);
}

void MainWindow::createActions()
{
    m_openAction = new QAction(tr("&Открыть…"), this);
    m_openAction->setIconText(tr("Открыть"));
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("Открыть PDF-документ"));
    m_openAction->setToolTip(tr("Открыть PDF-документ"));
    m_openAction->setIcon(themedIcon(QStringLiteral("document-open"),
                                     QStyle::SP_DialogOpenButton));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::chooseFile);

    m_printAction = new QAction(tr("&Печать…"), this);
    m_printAction->setIconText(tr("Печать"));
    m_printAction->setShortcut(QKeySequence::Print);
    m_printAction->setStatusTip(tr("Напечатать текущий документ через CUPS"));
    m_printAction->setToolTip(tr("Напечатать текущий документ"));
    m_printAction->setIcon(themedIcon(QStringLiteral("document-print")));
    connect(m_printAction, &QAction::triggered, this, &MainWindow::printDocument);

    m_documentInfoAction = new QAction(tr("С&войства документа…"), this);
    m_documentInfoAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    m_documentInfoAction->setStatusTip(tr("Показать сведения о документе и странице"));
    m_documentInfoAction->setIcon(themedIcon(QStringLiteral("document-properties"),
                                             QStyle::SP_MessageBoxInformation));
    connect(m_documentInfoAction, &QAction::triggered,
            this, &MainWindow::showDocumentProperties);

    m_quitAction = new QAction(tr("В&ыход"), this);
    m_quitAction->setIconText(tr("Выход"));
    m_quitAction->setShortcut(QKeySequence::Quit);
    m_quitAction->setStatusTip(tr("Закрыть pdfly"));
    m_quitAction->setIcon(themedIcon(QStringLiteral("application-exit"),
                                     QStyle::SP_DialogCloseButton));
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_firstPageAction = new QAction(tr("В &начало"), this);
    m_firstPageAction->setIconText(tr("В начало"));
    m_firstPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Home")));
    m_firstPageAction->setStatusTip(tr("Перейти к первой странице"));
    m_firstPageAction->setIcon(themedIcon(QStringLiteral("go-first"),
                                          QStyle::SP_MediaSkipBackward));
    connect(m_firstPageAction, &QAction::triggered, this, &MainWindow::goToFirstPage);

    m_previousPageAction = new QAction(tr("&Предыдущая страница"), this);
    m_previousPageAction->setIconText(tr("Назад"));
    m_previousPageAction->setShortcut(QKeySequence(Qt::Key_PageUp));
    m_previousPageAction->setStatusTip(tr("Перейти к предыдущей странице"));
    m_previousPageAction->setToolTip(tr("Предыдущая страница"));
    m_previousPageAction->setIcon(themedIcon(QStringLiteral("go-previous"),
                                             QStyle::SP_MediaSeekBackward));
    connect(m_previousPageAction, &QAction::triggered, this, &MainWindow::goToPreviousPage);

    m_nextPageAction = new QAction(tr("&Следующая страница"), this);
    m_nextPageAction->setIconText(tr("Вперёд"));
    m_nextPageAction->setShortcut(QKeySequence(Qt::Key_PageDown));
    m_nextPageAction->setStatusTip(tr("Перейти к следующей странице"));
    m_nextPageAction->setToolTip(tr("Следующая страница"));
    m_nextPageAction->setIcon(themedIcon(QStringLiteral("go-next"),
                                         QStyle::SP_MediaSeekForward));
    connect(m_nextPageAction, &QAction::triggered, this, &MainWindow::goToNextPage);

    m_lastPageAction = new QAction(tr("В &конец"), this);
    m_lastPageAction->setIconText(tr("В конец"));
    m_lastPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+End")));
    m_lastPageAction->setStatusTip(tr("Перейти к последней странице"));
    m_lastPageAction->setIcon(themedIcon(QStringLiteral("go-last"),
                                         QStyle::SP_MediaSkipForward));
    connect(m_lastPageAction, &QAction::triggered, this, &MainWindow::goToLastPage);

    m_zoomInAction = new QAction(tr("&Увеличить"), this);
    m_zoomInAction->setIconText(tr("Увеличить"));
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setStatusTip(tr("Увеличить масштаб"));
    m_zoomInAction->setToolTip(tr("Увеличить масштаб"));
    m_zoomInAction->setIcon(themedIcon(QStringLiteral("zoom-in")));
    connect(m_zoomInAction, &QAction::triggered, m_pageView, &PageView::zoomIn);

    m_zoomOutAction = new QAction(tr("У&меньшить"), this);
    m_zoomOutAction->setIconText(tr("Уменьшить"));
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setStatusTip(tr("Уменьшить масштаб"));
    m_zoomOutAction->setToolTip(tr("Уменьшить масштаб"));
    m_zoomOutAction->setIcon(themedIcon(QStringLiteral("zoom-out")));
    connect(m_zoomOutAction, &QAction::triggered, m_pageView, &PageView::zoomOut);

    m_actualSizeAction = new QAction(tr("Масштаб &100%"), this);
    m_actualSizeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    m_actualSizeAction->setStatusTip(tr("Показать страницу в масштабе 100%"));
    m_actualSizeAction->setIcon(themedIcon(QStringLiteral("zoom-original")));
    connect(m_actualSizeAction, &QAction::triggered, this, [this]
    {
        m_pageView->setZoomFactor(1.0);
    });

    m_fitWidthAction = new QAction(tr("По &ширине"), this);
    m_fitWidthAction->setIconText(tr("По ширине"));
    m_fitWidthAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+8")));
    m_fitWidthAction->setStatusTip(tr("Вписать страницу по ширине окна"));
    m_fitWidthAction->setToolTip(tr("Вписать страницу по ширине окна"));
    m_fitWidthAction->setIcon(themedIcon(QStringLiteral("zoom-fit-width")));
    connect(m_fitWidthAction, &QAction::triggered, m_pageView, &PageView::fitWidth);

    m_fitPageAction = new QAction(tr("По &странице"), this);
    m_fitPageAction->setIconText(tr("По странице"));
    m_fitPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+9")));
    m_fitPageAction->setStatusTip(tr("Вписать страницу целиком в окно"));
    m_fitPageAction->setToolTip(tr("Вписать страницу целиком в окно"));
    m_fitPageAction->setIcon(themedIcon(QStringLiteral("zoom-fit-best")));
    connect(m_fitPageAction, &QAction::triggered, m_pageView, &PageView::fitPage);

    m_rotateLeftAction = new QAction(tr("Повернуть &влево"), this);
    m_rotateLeftAction->setIconText(tr("Влево"));
    m_rotateLeftAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    m_rotateLeftAction->setStatusTip(tr("Повернуть текущую страницу против часовой стрелки"));
    m_rotateLeftAction->setToolTip(tr("Повернуть страницу против часовой стрелки"));
    m_rotateLeftAction->setIcon(themedIcon(QStringLiteral("object-rotate-left"),
                                           QStyle::SP_ArrowLeft));
    connect(m_rotateLeftAction, &QAction::triggered,
            m_pageView, &PageView::rotateCounterClockwise);

    m_rotateRightAction = new QAction(tr("Повернуть &вправо"), this);
    m_rotateRightAction->setIconText(tr("Вправо"));
    m_rotateRightAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    m_rotateRightAction->setStatusTip(tr("Повернуть текущую страницу по часовой стрелке"));
    m_rotateRightAction->setToolTip(tr("Повернуть страницу по часовой стрелке"));
    m_rotateRightAction->setIcon(themedIcon(QStringLiteral("object-rotate-right"),
                                            QStyle::SP_ArrowRight));
    connect(m_rotateRightAction, &QAction::triggered,
            m_pageView, &PageView::rotateClockwise);

    m_resetRotationAction = new QAction(tr("&Сбросить поворот"), this);
    m_resetRotationAction->setIconText(tr("Сброс"));
    m_resetRotationAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+R")));
    m_resetRotationAction->setStatusTip(tr("Вернуть исходную ориентацию страницы"));
    m_resetRotationAction->setIcon(themedIcon(QStringLiteral("edit-undo")));
    connect(m_resetRotationAction, &QAction::triggered,
            m_pageView, &PageView::resetRotation);

    m_fullScreenAction = new QAction(tr("Полноэкранный &режим"), this);
    m_fullScreenAction->setIconText(tr("Во весь экран"));
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setStatusTip(tr("Включить или выключить полноэкранный режим"));
    m_fullScreenAction->setToolTip(tr("Полноэкранный режим (F11)"));
    m_fullScreenAction->setIcon(themedIcon(QStringLiteral("view-fullscreen"),
                                           QStyle::SP_TitleBarMaxButton));
    connect(m_fullScreenAction, &QAction::toggled, this, [this](bool fullScreen)
    {
        if (fullScreen)
        {
            showFullScreen();
        }
        else
        {
            showNormal();
        }
    });

    m_exitFullScreenAction = new QAction(this);
    m_exitFullScreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
    m_exitFullScreenAction->setEnabled(false);
    connect(m_exitFullScreenAction, &QAction::triggered, this, [this]
    {
        m_fullScreenAction->setChecked(false);
    });
    addAction(m_exitFullScreenAction);
}

void MainWindow::createMenusAndToolBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&Файл"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_printAction);
    fileMenu->addAction(m_documentInfoAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu* goMenu = menuBar()->addMenu(tr("П&ереход"));
    goMenu->addAction(m_firstPageAction);
    goMenu->addAction(m_previousPageAction);
    goMenu->addAction(m_nextPageAction);
    goMenu->addAction(m_lastPageAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&Вид"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitWidthAction);
    viewMenu->addAction(m_fitPageAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_rotateLeftAction);
    viewMenu->addAction(m_rotateRightAction);
    viewMenu->addAction(m_resetRotationAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fullScreenAction);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Справка"));
    QAction* aboutAction = helpMenu->addAction(tr("&О программе pdfly"));
    connect(aboutAction, &QAction::triggered, this, [this]
    {
        QMessageBox::about(
            this, tr("О программе pdfly"),
            tr("<h3>pdfly %1</h3>"
               "<p>Лёгкий просмотрщик PDF с поддержкой статических XFA-форм.</p>"
               "<p>Создан на Qt 6 и PDF4QT Core. Распространяется по лицензии MIT.</p>"
               "<p>Поворот страниц влияет только на отображение и не изменяет "
               "исходный файл.</p>")
                .arg(QApplication::applicationVersion()));
    });

    QToolBar* toolBar = addToolBar(tr("Основная"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setIconSize(QSize(18, 18));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 3px; padding: 3px 6px; }"));
    toolBar->addAction(m_openAction);
    toolBar->addAction(m_printAction);
    toolBar->addSeparator();
    toolBar->addAction(m_previousPageAction);

    QLabel* pageLabel = new QLabel(tr("Стр."), toolBar);
    pageLabel->setContentsMargins(8, 0, 4, 0);
    toolBar->addWidget(pageLabel);

    m_pageSpinBox = new QSpinBox(toolBar);
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setKeyboardTracking(false);
    m_pageSpinBox->setEnabled(false);
    m_pageSpinBox->setMaximumWidth(64);
    toolBar->addWidget(m_pageSpinBox);

    m_pageCountLabel = new QLabel(tr("из 0"), toolBar);
    m_pageCountLabel->setContentsMargins(4, 0, 4, 0);
    toolBar->addWidget(m_pageCountLabel);
    toolBar->addAction(m_nextPageAction);
    toolBar->addSeparator();
    toolBar->addAction(m_zoomOutAction);

    m_zoomLabel = new QLabel(tr("100%"), toolBar);
    m_zoomLabel->setMinimumWidth(44);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    toolBar->addWidget(m_zoomLabel);

    toolBar->addAction(m_zoomInAction);
    toolBar->addSeparator();
    toolBar->addAction(m_fitPageAction);
    toolBar->addSeparator();
    toolBar->addAction(m_rotateLeftAction);
    toolBar->addAction(m_rotateRightAction);

    connect(m_pageSpinBox, &QSpinBox::valueChanged, this, [this](int value)
    {
        if (m_model->isOpen() && m_pageView->currentPage() != value - 1)
        {
            m_pageView->setPage(value - 1);
        }
    });
}

bool MainWindow::openFile(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    QString errorMessage;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool loaded = m_model->load(filePath, this, &errorMessage);
    QApplication::restoreOverrideCursor();

    if (!loaded)
    {
        QMessageBox::critical(this, tr("Ошибка открытия PDF"),
                              errorMessage.isEmpty() ? tr("Не удалось открыть документ.")
                                                     : errorMessage);
        return false;
    }

    updateDocumentUi();
    statusBar()->showMessage(tr("Открыт файл %1.").arg(QFileInfo(filePath).fileName()), 4000);
    return true;
}

void MainWindow::chooseFile()
{
    QString startDirectory = QDir::homePath();
    if (m_model->isOpen())
    {
        startDirectory = QFileInfo(m_model->filePath()).absolutePath();
    }

    QFileDialog dialog(this, tr("Открыть PDF"), startDirectory,
                       tr("Документы PDF (*.pdf *.PDF);;Все файлы (*)"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setLabelText(QFileDialog::LookIn, tr("Папка:"));
    dialog.setLabelText(QFileDialog::FileName, tr("Имя файла:"));
    dialog.setLabelText(QFileDialog::FileType, tr("Тип файлов:"));
    dialog.setLabelText(QFileDialog::Accept, tr("Открыть"));
    dialog.setLabelText(QFileDialog::Reject, tr("Отмена"));

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QStringList selectedFiles = dialog.selectedFiles();
    if (!selectedFiles.isEmpty())
    {
        openFile(selectedFiles.first());
    }
}

void MainWindow::printDocument()
{
    if (!m_model->isOpen())
    {
        return;
    }

    PrintDialog dialog(m_model, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    PrintSettings settings = dialog.settings();
    QString errorMessage;
    PrintRunResult result = runPrintJob(*m_model, settings, this, &errorMessage);

    if (result == PrintRunResult::Failed && !settings.flatten)
    {
        QMessageBox fallbackBox(this);
        fallbackBox.setIcon(QMessageBox::Question);
        fallbackBox.setWindowTitle(tr("Прямая печать не удалась"));
        fallbackBox.setText(tr("Не удалось отправить исходный PDF в CUPS:\n\n%1\n\n"
                               "Попробовать растровый режим? Он сохраняет поля XFA, их "
                               "оформление, аннотации и края страницы, но текст перестанет "
                               "быть векторным.")
                                .arg(errorMessage));
        QPushButton* retryButton = fallbackBox.addButton(
            tr("Растрировать"), QMessageBox::AcceptRole);
        fallbackBox.addButton(tr("Отмена"), QMessageBox::RejectRole);
        fallbackBox.setDefaultButton(retryButton);
        fallbackBox.exec();

        if (fallbackBox.clickedButton() == retryButton)
        {
            settings.flatten = true;
            settings.dpi = 300;
            settings.pageIndices.clear();
            for (int i = 0; i < m_model->pageCount(); ++i)
            {
                settings.pageIndices.push_back(i);
            }

            result = runPrintJob(*m_model, settings, this, &errorMessage);
        }
        else
        {
            statusBar()->showMessage(tr("Печать не выполнена."), 6000);
            return;
        }
    }

    if (result == PrintRunResult::Success)
    {
        statusBar()->showMessage(tr("Задание отправлено на %1.").arg(settings.printer), 7000);
    }
    else if (result == PrintRunResult::Cancelled)
    {
        statusBar()->showMessage(tr("Печать отменена."), 4000);
    }
    else
    {
        QMessageBox::critical(this, tr("Ошибка печати"),
                              errorMessage.isEmpty() ? tr("Не удалось отправить задание на печать.")
                                                     : errorMessage);
    }
}

void MainWindow::showDocumentProperties()
{
    if (!m_model->isOpen())
    {
        return;
    }

    const QFileInfo fileInfo(m_model->filePath());
    const QLocale russian(QLocale::Russian, QLocale::Russia);
    const QString fileSize = russian.formattedDataSize(
        fileInfo.size(), 1, QLocale::DataSizeTraditionalFormat);

    const QSizeF pageSize = m_model->pageSizeMm(0);
    const QString pageSizeText = pageSize.isEmpty()
        ? tr("неизвестно")
        : tr("%1 × %2 мм").arg(russian.toString(pageSize.width(), 'f', 1),
                               russian.toString(pageSize.height(), 'f', 1));

    const QString yesText = tr("да");
    const QString noText = tr("нет");
    const int pageCount = m_model->pageCount();

    const QString details = tr(
        "<h3>%1</h3>"
        "<p><b>Путь:</b> %2</p>"
        "<table cellspacing=\"4\">"
        "<tr><td><b>Размер файла:</b></td><td>%3</td></tr>"
        "<tr><td><b>Страниц:</b></td><td>%4</td></tr>"
        "<tr><td><b>Первая страница:</b></td><td>%5</td></tr>"
        "<tr><td><b>Статическая XFA:</b></td><td>%6</td></tr>"
        "<tr><td><b>Шифрование:</b></td><td>%7</td></tr>"
        "</table>"
        "<p><i>Поворот страницы влияет только на просмотр и не изменяет файл.</i></p>")
        .arg(fileInfo.fileName().toHtmlEscaped(),
             fileInfo.absoluteFilePath().toHtmlEscaped(),
             fileSize,
             QString::number(pageCount),
             pageSizeText,
             m_model->hasXfaForm() ? yesText : noText,
             m_model->isEncrypted() ? yesText : noText);

    QMessageBox box(QMessageBox::Information,
                    tr("Свойства документа"),
                    details,
                    QMessageBox::Ok,
                    this);
    box.setTextFormat(Qt::RichText);
    box.exec();
}

void MainWindow::goToFirstPage()
{
    m_pageView->setPage(0);
}

void MainWindow::goToPreviousPage()
{
    m_pageView->setPage(m_pageView->currentPage() - 1);
}

void MainWindow::goToNextPage()
{
    m_pageView->setPage(m_pageView->currentPage() + 1);
}

void MainWindow::goToLastPage()
{
    if (m_model->isOpen())
    {
        m_pageView->setPage(m_model->pageCount() - 1);
    }
}

void MainWindow::updateDocumentUi()
{
    const bool isOpen = m_model->isOpen();
    const int pageCount = m_model->pageCount();

    m_printAction->setEnabled(isOpen);
    m_documentInfoAction->setEnabled(isOpen);
    m_pageSpinBox->setEnabled(isOpen && pageCount > 0);
    m_pageSpinBox->setRange(1, qMax(1, pageCount));

    {
        const QSignalBlocker blocker(m_pageSpinBox);
        m_pageSpinBox->setValue(m_pageView->currentPage() + 1);
    }

    m_pageCountLabel->setText(tr("из %1").arg(pageCount));

    const bool hasXfa = isOpen && m_model->hasXfaForm();
    m_xfaLabel->setVisible(hasXfa);

    if (isOpen)
    {
        const QString fileName = QFileInfo(m_model->filePath()).fileName();
        setWindowTitle(tr("%1%2 — pdfly")
                           .arg(fileName, hasXfa ? tr(" [XFA]") : QString()));
    }
    else
    {
        setWindowTitle(tr("pdfly"));
    }

    updatePageUi(m_pageView->currentPage());
    updateZoomUi(m_pageView->zoomFactor());
}

void MainWindow::updatePageUi(int pageIndex)
{
    const int pageCount = m_model->isOpen() ? m_model->pageCount() : 0;
    if (pageCount <= 0)
    {
        m_pageStatusLabel->setText(tr("Документ не открыт"));
        updateNavigationActions();
        updateRotationUi(0);
        return;
    }

    const int page = qBound(0, pageIndex, pageCount - 1);
    {
        const QSignalBlocker blocker(m_pageSpinBox);
        m_pageSpinBox->setValue(page + 1);
    }

    updateNavigationActions();
    updateRotationUi(m_pageView->rotation());
}

void MainWindow::updateNavigationActions()
{
    const bool isOpen = m_model->isOpen();
    const int pageCount = m_model->pageCount();
    const int page = m_pageView->currentPage();

    m_firstPageAction->setEnabled(isOpen && page > 0);
    m_previousPageAction->setEnabled(isOpen && page > 0);
    m_nextPageAction->setEnabled(isOpen && page + 1 < pageCount);
    m_lastPageAction->setEnabled(isOpen && page + 1 < pageCount);
    m_pageSpinBox->setEnabled(isOpen && pageCount > 0);
}

void MainWindow::updateZoomUi(qreal zoom)
{
    if (m_zoomLabel)
    {
        m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(zoom * 100.0)));
    }
}

void MainWindow::updateRotationUi(int rotation)
{
    const bool isOpen = m_model->isOpen();
    m_rotateLeftAction->setEnabled(isOpen);
    m_rotateRightAction->setEnabled(isOpen);
    m_resetRotationAction->setEnabled(isOpen && rotation != 0);

    const int pageCount = isOpen ? m_model->pageCount() : 0;
    if (pageCount <= 0)
    {
        return;
    }

    QString status = tr("Страница %1 из %2")
                         .arg(m_pageView->currentPage() + 1)
                         .arg(pageCount);
    if (rotation != 0)
    {
        status += tr(" · поворот %1°").arg(rotation);
    }
    m_pageStatusLabel->setText(status);
}

void MainWindow::showRenderFailure(const QString& message)
{
    const QString firstLine = message.section(QLatin1Char('\n'), 0, 0);
    statusBar()->showMessage(
        firstLine.isEmpty() ? tr("Не удалось отобразить страницу.") : firstLine, 8000);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
    {
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls)
    {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls)
    {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        {
            openFile(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);

    if (event->type() != QEvent::WindowStateChange)
    {
        return;
    }

    if (m_exitFullScreenAction)
    {
        m_exitFullScreenAction->setEnabled(isFullScreen());
    }
    if (m_fullScreenAction)
    {
        const QSignalBlocker blocker(m_fullScreenAction);
        m_fullScreenAction->setChecked(isFullScreen());
    }
}
