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
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QtMath>

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent)
{
    setAcceptDrops(true);
    resize(1100, 850);
    setWindowTitle(tr("PDF Viewer"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("application-pdf")));

    m_model = new PdfDocumentModel(this);
    m_pageView = new PageView(this);
    setCentralWidget(m_pageView);
    m_pageView->setModel(m_model);

    createActions();
    createMenusAndToolBar();

    m_pageStatusLabel = new QLabel(tr("No document"), this);
    m_xfaLabel = new QLabel(tr("Static XFA (read-only)"), this);
    m_xfaLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_xfaLabel->setVisible(false);
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
    connect(m_pageView, &PageView::renderFailed,
            this, &MainWindow::showRenderFailure);

    updateDocumentUi();
    updateZoomUi(m_pageView->zoomFactor());
}

void MainWindow::createActions()
{
    m_openAction = new QAction(tr("&Open..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("Open a PDF document"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::chooseFile);

    m_printAction = new QAction(tr("&Print..."), this);
    m_printAction->setShortcut(QKeySequence::Print);
    m_printAction->setStatusTip(tr("Print the current document through CUPS"));
    connect(m_printAction, &QAction::triggered, this, &MainWindow::printDocument);

    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_firstPageAction = new QAction(tr("&First Page"), this);
    m_firstPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Home")));
    connect(m_firstPageAction, &QAction::triggered, this, &MainWindow::goToFirstPage);

    m_previousPageAction = new QAction(tr("&Previous Page"), this);
    m_previousPageAction->setShortcut(QKeySequence(Qt::Key_PageUp));
    connect(m_previousPageAction, &QAction::triggered, this, &MainWindow::goToPreviousPage);

    m_nextPageAction = new QAction(tr("&Next Page"), this);
    m_nextPageAction->setShortcut(QKeySequence(Qt::Key_PageDown));
    connect(m_nextPageAction, &QAction::triggered, this, &MainWindow::goToNextPage);

    m_lastPageAction = new QAction(tr("&Last Page"), this);
    m_lastPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+End")));
    connect(m_lastPageAction, &QAction::triggered, this, &MainWindow::goToLastPage);

    m_zoomInAction = new QAction(tr("Zoom &In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, m_pageView, &PageView::zoomIn);

    m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, m_pageView, &PageView::zoomOut);

    m_actualSizeAction = new QAction(tr("&Actual Size"), this);
    m_actualSizeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(m_actualSizeAction, &QAction::triggered, this, [this]
    {
        m_pageView->setZoomFactor(1.0);
    });

    m_fitWidthAction = new QAction(tr("Fit &Width"), this);
    m_fitWidthAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+8")));
    connect(m_fitWidthAction, &QAction::triggered, m_pageView, &PageView::fitWidth);

    m_fitPageAction = new QAction(tr("Fit &Page"), this);
    m_fitPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+9")));
    connect(m_fitPageAction, &QAction::triggered, m_pageView, &PageView::fitPage);
}

void MainWindow::createMenusAndToolBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_printAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu* goMenu = menuBar()->addMenu(tr("&Go"));
    goMenu->addAction(m_firstPageAction);
    goMenu->addAction(m_previousPageAction);
    goMenu->addAction(m_nextPageAction);
    goMenu->addAction(m_lastPageAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitWidthAction);
    viewMenu->addAction(m_fitPageAction);

    QToolBar* toolBar = addToolBar(tr("Main"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar->addAction(m_openAction);
    toolBar->addAction(m_printAction);
    toolBar->addSeparator();
    toolBar->addAction(m_firstPageAction);
    toolBar->addAction(m_previousPageAction);

    toolBar->addWidget(new QLabel(tr(" Page "), toolBar));
    m_pageSpinBox = new QSpinBox(toolBar);
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setKeyboardTracking(false);
    m_pageSpinBox->setEnabled(false);
    m_pageSpinBox->setMaximumWidth(80);
    toolBar->addWidget(m_pageSpinBox);

    m_pageCountLabel = new QLabel(tr(" of 0 "), toolBar);
    toolBar->addWidget(m_pageCountLabel);
    toolBar->addAction(m_nextPageAction);
    toolBar->addAction(m_lastPageAction);
    toolBar->addSeparator();
    toolBar->addAction(m_zoomOutAction);

    m_zoomLabel = new QLabel(tr("100%"), toolBar);
    m_zoomLabel->setMinimumWidth(48);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    toolBar->addWidget(m_zoomLabel);

    toolBar->addAction(m_zoomInAction);
    toolBar->addAction(m_actualSizeAction);
    toolBar->addSeparator();
    toolBar->addAction(m_fitWidthAction);
    toolBar->addAction(m_fitPageAction);

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
        QMessageBox::critical(this, tr("Open PDF"),
                              errorMessage.isEmpty() ? tr("The document could not be opened.")
                                                     : errorMessage);
        return false;
    }

    updateDocumentUi();
    statusBar()->showMessage(tr("Opened %1.").arg(QFileInfo(filePath).fileName()), 4000);
    return true;
}

void MainWindow::chooseFile()
{
    QString startDirectory = QDir::homePath();
    if (m_model->isOpen())
    {
        startDirectory = QFileInfo(m_model->filePath()).absolutePath();
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open PDF"), startDirectory,
        tr("PDF documents (*.pdf *.PDF);;All files (*)"));

    if (!filePath.isEmpty())
    {
        openFile(filePath);
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
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Direct printing failed"),
            tr("Sending the original PDF to CUPS failed:\n\n%1\n\n"
               "Try the rasterized flatten fallback? It preserves XFA fields, form "
               "appearances, annotations, and page edges, but text is no longer vector.")
                .arg(errorMessage),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

        if (answer == QMessageBox::Yes)
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
            statusBar()->showMessage(tr("Printing failed."), 6000);
            return;
        }
    }

    if (result == PrintRunResult::Success)
    {
        statusBar()->showMessage(tr("Print job sent to %1.").arg(settings.printer), 7000);
    }
    else if (result == PrintRunResult::Cancelled)
    {
        statusBar()->showMessage(tr("Printing cancelled."), 4000);
    }
    else
    {
        QMessageBox::critical(this, tr("Printing failed"),
                              errorMessage.isEmpty() ? tr("The print job failed.")
                                                     : errorMessage);
    }
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
    m_pageSpinBox->setEnabled(isOpen && pageCount > 0);
    m_pageSpinBox->setRange(1, qMax(1, pageCount));

    {
        const QSignalBlocker blocker(m_pageSpinBox);
        m_pageSpinBox->setValue(m_pageView->currentPage() + 1);
    }

    m_pageCountLabel->setText(tr(" of %1 ").arg(pageCount));

    const bool hasXfa = isOpen && m_model->hasXfaForm();
    m_xfaLabel->setVisible(hasXfa);

    if (isOpen)
    {
        const QString fileName = QFileInfo(m_model->filePath()).fileName();
        setWindowTitle(tr("%1%2 - PDF Viewer")
                           .arg(fileName, hasXfa ? tr(" [XFA]") : QString()));
    }
    else
    {
        setWindowTitle(tr("PDF Viewer"));
    }

    updatePageUi(m_pageView->currentPage());
    updateZoomUi(m_pageView->zoomFactor());
}

void MainWindow::updatePageUi(int pageIndex)
{
    const int pageCount = m_model->isOpen() ? m_model->pageCount() : 0;
    if (pageCount <= 0)
    {
        m_pageStatusLabel->setText(tr("No document"));
        updateNavigationActions();
        return;
    }

    const int page = qBound(0, pageIndex, pageCount - 1);
    {
        const QSignalBlocker blocker(m_pageSpinBox);
        m_pageSpinBox->setValue(page + 1);
    }

    m_pageStatusLabel->setText(tr("Page %1 of %2").arg(page + 1).arg(pageCount));
    updateNavigationActions();
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

void MainWindow::showRenderFailure(const QString& message)
{
    const QString firstLine = message.section(QLatin1Char('\n'), 0, 0);
    statusBar()->showMessage(firstLine.isEmpty() ? tr("Page rendering failed.") : firstLine, 8000);
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
